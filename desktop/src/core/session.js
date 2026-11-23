const fs = require("node:fs/promises");
const path = require("node:path");
const net = require("node:net");
const { spawn } = require("node:child_process");
const { EventEmitter } = require("node:events");
const { QMP } = require("./qmp");
const IMAGES = Object.freeze({
  daisy: "flower.bin",
  rose: "rose.bin",
  factory: "factory_v2.bin",
});
// Owns one emulator session; usable by Electron, CLI and Node tests.
function createSession(config) {
  const events = new EventEmitter();
  const imported = new Map();
  const ROOT = config.root,
    OUT = config.output;
  let child,
    qmp,
    frameTimer,
    logTimer,
    busy = false,
    stopping = false,
    current = "daisy",
    touchPort,
    qmpPort,
    lastLog = "",
    frameCount = 0;
  const pause = (ms) => new Promise((r) => setTimeout(r, ms));
  function send(channel, data) {
    events.emit(channel, data);
  }
  function status(state, detail = "") {
    send("status", { state, detail, current });
  }
  async function freePort() {
    return new Promise((resolve, reject) => {
      const s = net.createServer();
      s.on("error", reject);
      s.listen(0, "127.0.0.1", () => {
        const p = s.address().port;
        s.close(() => resolve(p));
      });
    });
  }
  async function connectQmp() {
    for (let i = 0; i < 60; i++) {
      if (!child) throw Error("模拟器提前退出，请查看日志");
      try {
        const sock = await new Promise((resolve, reject) => {
          const s = net.createConnection({ host: "127.0.0.1", port: qmpPort });
          s.once("connect", () => resolve(s));
          s.once("error", (e) => {
            s.destroy();
            reject(e);
          });
        });
        const q = new QMP(sock);
        await q.call("qmp_capabilities");
        return q;
      } catch (e) {
        await pause(100);
      }
    }
    throw Error("无法连接 QEMU 控制接口");
  }
  async function readLogs() {
    let a = "",
      b = "";
    try {
      a = await fs.readFile(path.join(OUT, "serial.log"), "utf8");
    } catch {}
    try {
      b = await fs.readFile(path.join(OUT, "qemu.stderr.log"), "utf8");
    } catch {}
    const text = a.slice(-24000) + "\n" + b.slice(-3000);
    if (text !== lastLog) {
      lastLog = text;
      send("logs", text);
    }
  }
  async function frame() {
    if (busy || !qmp) return;
    busy = true;
    const active = qmp;
    try {
      await active.call("screendump", {
        filename: path.join(OUT, "frame.ppm"),
        device: "co5300",
      });
      const b = await fs.readFile(path.join(OUT, "frame.ppm"));
      let off = 0;
      const lines = [];
      for (let i = 0; i < 3; i++) {
        const n = b.indexOf(10, off);
        lines.push(b.subarray(off, n).toString().trim());
        off = n + 1;
      }
      const [width, height] = lines[1].split(" ").map(Number);
      if (
        lines[0] !== "P6" ||
        width !== 368 ||
        height !== 448 ||
        lines[2] !== "255" ||
        b.length - off !== width * height * 3
      )
        throw Error("屏幕数据格式不正确");
      if (qmp === active) {
        send("frame", { width, height, rgb: b.subarray(off) });
        frameCount++;
      }
    } catch (e) {
      if (!stopping && qmp === active) send("frame-error", e.message);
    } finally {
      if (qmp === active) busy = false;
    }
  }
  async function stop() {
    stopping = true;
    clearInterval(frameTimer);
    clearInterval(logTimer);
    frameTimer = logTimer = null;
    if (qmp) {
      qmp.close();
      qmp = null;
    }
    const p = child;
    child = null;
    if (p) {
      p.kill("SIGTERM");
      await Promise.race([new Promise((r) => p.once("exit", r)), pause(2000)]);
      if (p.exitCode === null) p.kill("SIGKILL");
    }
    busy = false;
    stopping = false;
    status("stopped", "已停止");
  }
  async function start(key) {
    const imagePath = Object.hasOwn(IMAGES, key)
      ? path.join(config.images, IMAGES[key])
      : imported.get(key);
    if (!imagePath) throw Error("未知固件");
    await stop();
    current = key;
    frameCount = 0;
    lastLog = "";
    status("starting", "启动固件中");
    const exe = config.executable;
    await fs.access(exe);
    await fs.mkdir(OUT, { recursive: true });
    await fs.writeFile(path.join(OUT, "qemu.stderr.log"), "");
    await fs.writeFile(path.join(OUT, "serial.log"), "");
    touchPort = await freePort();
    qmpPort = await freePort();
    const args = [
      "-L", config.bios,
      "-display",
      "none",
      "-monitor",
      "none",
      "-qmp",
      `tcp:127.0.0.1:${qmpPort},server=on,wait=off`,
      "-serial",
      `file:${path.join(OUT, "serial.log")}`,
      "-machine",
      "esp32s3",
      "-m",
      "8M",
      "-global",
      "driver=ssi_psram,property=is_octal,value=true",
      "-drive",
      `file=${imagePath.replaceAll(",", ",,")},if=mtd,format=raw,snapshot=on`,
      "-global",
      "driver=esp32s3.gpio,property=strap_mode,value=0x04",
      "-chardev",
      `socket,id=cst816s-touch,host=127.0.0.1,port=${touchPort},server=on,wait=off`,
    ];
    const p = (child = spawn(exe, args, {
      cwd: ROOT,
      stdio: ["ignore", "pipe", "pipe"],
    }));
    for (const stream of [p.stdout, p.stderr])
      stream.on("data", (b) =>
        fs.appendFile(path.join(OUT, "qemu.stderr.log"), b).catch(() => {}),
      );
    p.on("error", (e) => status("error", e.message));
    p.on("exit", (code, signal) => {
      if (child === p) {
        child = null;
        clearInterval(frameTimer);
        clearInterval(logTimer);
        readLogs();
        status("error", `QEMU 已退出（${code ?? signal}）`);
      }
    });
    qmp = await connectQmp();
    status("running", "固件运行中");
    frameTimer = setInterval(frame, 120);
    logTimer = setInterval(readLogs, 600);
    await frame();
  }
  function inject({ x, y, press }) {
    if (!child || !Number.isFinite(x) || !Number.isFinite(y)) return;
    return new Promise((resolve) => {
      const s = net.createConnection(touchPort, "127.0.0.1");
      s.setTimeout(1000, () => s.destroy());
      s.on("connect", () =>
        s.end(
          `${Math.max(0, Math.min(367, Math.round(x)))},${Math.max(0, Math.min(447, Math.round(y)))},${press ? 1 : 0}\n`,
        ),
      );
      s.on("error", () => resolve());
      s.on("close", resolve);
    });
  }

  return {
    addFirmware: (item) => imported.set(item.id, item.path),
    events,
    start,
    stop,
    touch: inject,
    reset: async () => {
      if (qmp) await qmp.call("system_reset");
      else await start(current);
    },
    get frameCount() {
      return frameCount;
    },
    get current() {
      return current;
    },
  };
}
module.exports = { createSession, IMAGES };
