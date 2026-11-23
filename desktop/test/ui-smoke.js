const fs = require("node:fs/promises");
const path = require("node:path");
const assert = require("node:assert/strict");
const pause = (ms) => new Promise((r) => setTimeout(r, ms));
async function run({ win, session, root, output, images = path.join(root,"artifacts","flash_images") }) {
  const errors = [];
  // Explicit content size also makes screenshots comparable across desktop DPI settings.
  win.setContentSize(1220,850);

  win.webContents.on("console-message", (event) => {
    if (event.level === "error") errors.push(event.message);
  });
  const js = (code) => win.webContents.executeJavaScript(code);
  const click = (selector) =>
    js(`document.querySelector(${JSON.stringify(selector)}).click()`);
  async function ready(key) {
    for (let i = 0; i < 100; i++) {
      if (
        session.current === key &&
        (await js(
          "document.getElementById('state').classList.contains('running')",
        ))
      )
        return;
      await pause(100);
    }
    throw Error(`UI did not start ${key}`);
  }
  async function waitLog(text) {
    for (let i = 0; i < 120; i++) {
      if (
        (
          await fs
            .readFile(path.join(output, "serial.log"), "utf8")
            .catch(() => "")
        ).includes(text)
      )
        return;
      await pause(1000);
    }
    throw Error(`Firmware readiness timed out: ${text}`);
  }
  await session.start("daisy");
  await waitLog("PRESENT fps=");
  await pause(1500);
  const rect = await js(
    "(()=>{const r=document.getElementById('screen').getBoundingClientRect();return {x:Math.round(r.x+r.width/2),y:Math.round(r.y+r.height/2),width:r.width,height:r.height}})()",
  );
  assert(rect.width > 200 && rect.height > 250, "preview must be visible");
  await fs.writeFile(
    path.join(root, "output", "electron-ui.png"),
    (await win.webContents.capturePage()).toPNG(),
  );
  await js(
    "document.getElementById('screen').dispatchEvent(new PointerEvent('pointerdown', {clientX: " +
      rect.x +
      ",clientY: " +
      rect.y +
      ",pointerId:1,bubbles:true}))",
  );
  await pause(3500);
  await js(
    "document.getElementById('screen').dispatchEvent(new PointerEvent('pointerup', {pointerId:1,bubbles:true}))",
  );
  await pause(1500);
  const log = await fs.readFile(path.join(output, "serial.log"), "utf8");
  assert(
    log.includes("long_press close"),
    "renderer pointer must close flower",
  );
  assert(
    log.includes("release bloom"),
    "renderer pointer release must open flower",
  );
  assert(session.frameCount > 20, "screen must receive live frames");
  const daisyFrames = session.frameCount;
  await click('[data-key="rose"]');
  await ready("rose");
  await waitLog("PRESENT fps=");
  await pause(1500);
  assert(session.frameCount > 20);
  for (let i = 0; i < 60; i++) {
    if (
      (await fs.readFile(path.join(output, "serial.log"), "utf8")).includes(
        "PRESENT fps=",
      )
    )
      break;
    await pause(1000);
  }
  assert(
    (await fs.readFile(path.join(output, "serial.log"), "utf8")).includes(
      "PRESENT fps=",
    ),
    "rose must produce rendered frames",
  );
  await fs.writeFile(
    path.join(root, "output", "electron-rose.png"),
    (await win.webContents.capturePage()).toPNG(),
  );
  await click("#toggle");
  await pause(700);
  assert(
    await js("document.getElementById('state').classList.contains('stopped')"),
  );
  await click("#toggle");
  await ready("rose");
  await pause(2000);
  await click("#reset");
  await pause(2000);
  const hasFactory = await fs.access(path.join(images,"factory_v2.bin")).then(()=>true,()=>false);
  if(hasFactory){
  await click('[data-key="factory"]');
  await ready("factory");
  await waitLog("Brookesia firmware is ready");
  await pause(3000);
  assert(session.frameCount > 20);
  const factory = await fs.readFile(path.join(output, "serial.log"), "utf8");
  assert(!/Guru Meditation|Failed to mount.*SPIFFS|abort\(\)/i.test(factory));
  await fs.writeFile(
    path.join(root, "output", "electron-factory.png"),
    (await win.webContents.capturePage()).toPNG(),
  );
  }
  // Exercise the import button, IPC, validation, persistence and boot.
  // Only the operating-system file chooser is stubbed in automated tests.
  const { dialog } = require("electron"),
    originalChooser = dialog.showOpenDialog;
  try {
    dialog.showOpenDialog = async () => ({
      canceled: false,
      filePaths: [path.join(images,"rose.bin")],
    });
    await click("#import-firmware");
    const key = "import-3fa389329d2b4a93";
    await ready(key);
    await waitLog("PRESENT fps=");
    await pause(1500);
    assert(await js("!!document.querySelector('[data-key=\"" + key + "\"]')"));
    await fs.writeFile(
      path.join(root, "output", "electron-imported.png"),
      (await win.webContents.capturePage()).toPNG(),
    );
  } finally {
    dialog.showOpenDialog = originalChooser;
  }
  assert.deepEqual(errors, []);
  await fs.writeFile(
    path.join(root, "output", "electron-acceptance.json"),
    JSON.stringify(
      {
        passed: true,
        platform: process.platform,
        arch: process.arch,
        daisyFrames,
        hold: true,
        release: true,
        switchRose: true,
        switchFactory: hasFactory ? true : "not bundled",
        stopStart: true,
        reset: true,
        importFirmware: true,
        rendererErrors: errors,
      },
      null,
      2,
    ),
  );
}
module.exports = { run };
