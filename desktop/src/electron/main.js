const { app, BrowserWindow, ipcMain, shell, dialog } = require("electron");
const fs = require("node:fs/promises");
const path = require("node:path");
const { createSession } = require("../core/session");
const { runtimePaths } = require("../platform/paths");
const { createFirmwareStore } = require("../core/firmware");
app.setName("青芽");
const automated = process.argv.includes("--smoke-test") || process.argv.includes("--batch-test");
if (automated || process.env.QEMU_SOFTWARE_RENDERING === "1") app.disableHardwareAcceleration();
const ROOT = path.resolve(
  process.env.QEMU_PROJECT_ROOT ||
    (app.isPackaged ? process.resourcesPath : path.join(__dirname, "../../..")),
);
const OUT =
  process.env.QEMU_OUTPUT ||
  (app.isPackaged
    ? path.join(app.getPath("userData"), "sessions")
    : path.join(ROOT, "output", process.argv.includes("--batch-test") ? "batch-session" : process.argv.includes("--smoke-test") ? "smoke-session" : "desktop"));
const config = runtimePaths({ projectRoot: ROOT, outputDir: OUT });
const session = createSession(config);
const firmwareStore = createFirmwareStore(
  automated
    ? path.join(OUT, "test-imports")
    : path.join(app.getPath("userData"), "firmware"),
);
let importing = false;
ipcMain.handle("available-builtins", async () => {
  const available=[];
  for(const [key,file] of Object.entries(require('../core/session').IMAGES)) {
    try {await fs.access(path.join(config.images,file));available.push(key);} catch {}
  }
  return available;
});
ipcMain.handle("list-firmware", () => firmwareStore.list());
ipcMain.handle("import-firmware", async () => {
  if (importing) return { ok: false, error: "正在导入，请稍候" };
  importing = true;
  try {
    const choice = await dialog.showOpenDialog(win, {
      title: "导入微雪 V2 固件",
      properties: ["openFile"],
      filters: [
        {
          name: "完整 Flash BIN / ESP-IDF flasher_args.json",
          extensions: ["bin", "json"],
        },
      ],
    });
    if (choice.canceled) return { ok: true, canceled: true };
    const item = await firmwareStore.importFile(choice.filePaths[0]);
    session.addFirmware({ id: item.id, path: firmwareStore.imagePath(item) });
    return { ok: true, item };
  } catch (e) {
    return { ok: false, error: e.message };
  } finally {
    importing = false;
  }
});
let win,
  transition = false,
  quitting = false;
function send(channel, data) {
  if (win && !win.isDestroyed()) win.webContents.send(channel, data);
}
for (const channel of ["status", "frame", "logs", "frame-error"])
  session.events.on(channel, (data) => send(channel, data));
async function action(fn) {
  if (transition) return { ok: false, error: "正在切换设备，请稍候" };
  transition = true;
  try {
    await fn();
    return { ok: true };
  } catch (e) {
    await session.stop();
    send("status", {
      state: "error",
      detail: e.message,
      current: session.current,
    });
    return { ok: false, error: e.message };
  } finally {
    transition = false;
  }
}
ipcMain.handle("start", (_, key) => action(() => session.start(key)));
ipcMain.handle("stop", () => action(() => session.stop()));
ipcMain.handle("reset", () => action(() => session.reset()));
ipcMain.handle("touch", (_, point) => session.touch(point || {}));
ipcMain.handle("open-folder", () => shell.openPath(ROOT));
ipcMain.handle("save-screen", async (_, url) => {
  if (
    typeof url !== "string" ||
    !url.startsWith("data:image/png;base64,") ||
    url.length > 4000000
  )
    throw Error("无效截图");
  const result = await dialog.showSaveDialog(win, {
    defaultPath: path.join(
      app.getPath("pictures"),
      `${session.current}-screen.png`,
    ),
    filters: [{ name: "PNG", extensions: ["png"] }],
  });
  if (result.canceled) return null;
  await fs.writeFile(result.filePath, Buffer.from(url.split(",")[1], "base64"));
  return result.filePath;
});
app.on("before-quit", (e) => {
  if (!quitting) {
    e.preventDefault();
    quitting = true;
    session.stop().finally(() => app.quit());
  }
});
app.on("window-all-closed", () => app.quit());
app.whenReady().then(async () => {
  for (const item of await firmwareStore.list())
    session.addFirmware({ id: item.id, path: firmwareStore.imagePath(item) });
  const nativeStyle =
    process.platform === "darwin"
      ? {
          titleBarStyle: "hiddenInset",
          trafficLightPosition: { x: 22, y: 22 },
          vibrancy: "under-window",
          visualEffectState: "active",
        }
      : {};
  win = new BrowserWindow({
    show: !automated,
    width: 1220,
    height: 850,
    minWidth: 1060,
    minHeight: 740,
    title: "青芽 · 设备实验室",
    backgroundColor: "#e8eee8",
    ...nativeStyle,
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
      backgroundThrottling: false,
    },
  });
  win.webContents.setWindowOpenHandler(() => ({ action: "deny" }));
  win.webContents.on("will-navigate", (e) => e.preventDefault());
  await win.loadFile(path.join(__dirname, "../ui/index.html"));
  if (process.argv.includes("--batch-test")) {
    try {await require('../../test/batch-firmware').run({win,session,store:firmwareStore,root:ROOT,output:OUT,inventory:process.argv[process.argv.indexOf('--batch-test')+1]});app.quit();}
    catch(e){console.error(e);await session.stop();app.exit(1);}
  } else if (process.argv.includes("--smoke-test")) {
    try {
      await require("../../test/ui-smoke").run({
        win,
        session,
        root: ROOT,
        output: OUT,
        images: config.images,
      });
      app.quit();
    } catch (e) {
      console.error(e);
      await session.stop();
      app.exit(1);
    }
  } else await action(() => session.start("daisy"));
});
