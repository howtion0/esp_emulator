const $ = (id) => document.getElementById(id);
const canvas = $("screen"),
  ctx = canvas.getContext("2d", { alpha: false });
const names = {
  daisy: ["一朵小雏菊", "让花开在桌面上，也让每一次触碰都有回应。"],
  rose: ["极光玫瑰", "蓝白之间，一朵会回应触碰的花。"],
  factory: ["微雪桌面", "熟悉的设备界面，现在可以在桌面上交互。"],
};
let running = false,
  selected = "daisy",
  pressed = false,
  last = { x: 184, y: 220 },
  chain = Promise.resolve(),
  toastTimer,
  hasFrame = false;
function toast(text) {
  $("toast").textContent = text;
  $("toast").classList.add("show");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => $("toast").classList.remove("show"), 3000);
}
function selection(key) {
  selected = key;
  document
    .querySelectorAll(".firmware")
    .forEach((b) => b.classList.toggle("selected", b.dataset.key === key));
  $("name").firstChild.textContent = names[key][0];
  $("subtitle").textContent = names[key][1];
  $("gesture").textContent =
    key !== "daisy" && key !== "rose"
      ? "点击应用 · 拖动翻页 · 自由探索"
      : "长按收拢 · 松手绽放 · 拖动转向";
  $("fps").textContent = "—";
}
async function switchTo(key) {
  await release();
  selection(key);
  $("placeholder").style.display = "flex";
  hasFrame = false;
  const r = await window.device.start(key);
  if (!r.ok) toast(r.error);
}
document
  .querySelectorAll(".firmware")
  .forEach((b) => (b.onclick = () => switchTo(b.dataset.key)));
$("toggle").onclick = async () => {
  await release();
  if (running) await window.device.stop();
  else await switchTo(selected);
};
$("reset").onclick = async () => {
  await release();
  await window.device.reset();
};
$("folder").onclick = () => window.device.openFolder();
$("capture").onclick = async () => {
  if (!hasFrame) return toast("等待第一帧画面");
  const p = await window.device.saveScreen(canvas.toDataURL("image/png"));
  if (p) toast("屏幕截图已保存");
};
function point(e) {
  const r = canvas.getBoundingClientRect();
  return {
    x: Math.max(0, Math.min(367, ((e.clientX - r.left) * 368) / r.width)),
    y: Math.max(0, Math.min(447, ((e.clientY - r.top) * 448) / r.height)),
  };
}
function feedback(p, down) {
  $("touch-x").textContent = Math.round(p.x);
  $("touch-y").textContent = Math.round(p.y);
  $("touch-state").textContent = down ? "按住中" : "已松开";
  $("touch-point").style.cssText =
    `left:${(p.x / 368) * 100}%;top:${(p.y / 448) * 100}%;opacity:${down ? 1 : 0.3}`;
}
function input(p, down) {
  feedback(p, down);
  chain = chain
    .catch(() => {})
    .then(() => window.device.touch({ ...p, press: down }));
  return chain;
}
canvas.onpointerdown = (e) => {
  if (!running) return;
  pressed = true;
  last = point(e);
  if (e.isTrusted) canvas.setPointerCapture(e.pointerId);
  input(last, true);
};
let lastMove = 0;
canvas.onpointermove = (e) => {
  if (!pressed) return;
  last = point(e);
  if (performance.now() - lastMove > 35) {
    lastMove = performance.now();
    input(last, true);
  }
};
async function release() {
  if (pressed) {
    pressed = false;
    await input(last, false);
  }
}
canvas.onpointerup = release;
canvas.onpointercancel = release;
canvas.onlostpointercapture = release;
window.onblur = release;
window.device.onStatus((s) => {
  running = s.state === "running";
  selected = s.current;
  const pending = s.state === "starting";
  $("state").className = "state " + s.state;
  $("state").lastChild.textContent =
    {
      running: "运行中",
      starting: "启动中",
      stopped: "已停止",
      error: "连接异常",
    }[s.state] || s.state;
  $("detail").textContent = s.detail;
  $("toggle").innerHTML = running
    ? "<span>■</span> 停止设备"
    : "<span>▶</span> 启动设备";
  document
    .querySelectorAll(".firmware, #toggle, #reset")
    .forEach((b) => (b.disabled = pending));
  if (s.state === "error") toast(s.detail);
});
window.device.onFrame(({ width, height, rgb }) => {
  const image = ctx.createImageData(width, height);
  for (let p = 0, i = 0; p < rgb.length; p += 3, i += 4) {
    image.data[i] = rgb[p];
    image.data[i + 1] = rgb[p + 1];
    image.data[i + 2] = rgb[p + 2];
    image.data[i + 3] = 255;
  }
  ctx.putImageData(image, 0, 0);
  hasFrame = true;
  $("placeholder").style.display = "none";
});
window.device.onLogs((text) => {
  const el = $("logs"),
    atBottom = el.scrollHeight - el.scrollTop - el.clientHeight < 50;
  el.textContent = text.trim();
  if (atBottom) el.scrollTop = el.scrollHeight;
  const matches = [...text.matchAll(/PRESENT fps=([\d.]+)/g)];
  if (matches.length)
    $("fps").textContent = Number(matches.at(-1)[1]).toFixed(1);
  $("log-count").textContent = "LIVE";
});
window.device.onFrameError(() => {});

function addImported(item) {
  names[item.id] = [item.name, "已导入 · 微雪 V2 板级模型"];
  if (
    [...document.querySelectorAll(".firmware")].some(
      (el) => el.dataset.key === item.id,
    )
  )
    return;
  const button = document.createElement("button");
  button.className = "firmware";
  button.dataset.key = item.id;
  const icon = document.createElement("span");
  icon.className = "flower-symbol factory";
  icon.textContent = "▣";
  const label = document.createElement("span"),
    title = document.createElement("strong"),
    hint = document.createElement("small");
  title.textContent = item.name;
  hint.textContent = "本地固件 · 16 MB";
  label.append(title, hint);
  const mark = document.createElement("span");
  mark.className = "selection";
  mark.textContent = "✓";
  button.append(icon, label, mark);
  button.onclick = () => switchTo(item.id);
  $("imported-firmware").append(button);
}
window.device
  .listFirmware()
  .then((items) => items.forEach(addImported))
  .catch((e) => toast(e.message));
$("import-firmware").onclick = async () => {
  $("import-firmware").disabled = true;
  try {
    const result = await window.device.importFirmware();
    if (!result.ok) toast(result.error);
    else if (!result.canceled) {
      addImported(result.item);
      await switchTo(result.item.id);
    }
  } catch (e) {
    toast(e.message);
  } finally {
    $("import-firmware").disabled = false;
  }
};

window.device.availableBuiltins().then(keys=>{
  for(const button of document.querySelectorAll('.firmware')) {
    if(['daisy','rose','factory'].includes(button.dataset.key) && !keys.includes(button.dataset.key)) button.hidden=true;
  }
});
