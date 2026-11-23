const { contextBridge, ipcRenderer } = require("electron");
contextBridge.exposeInMainWorld("device", {
  availableBuiltins: () => ipcRenderer.invoke("available-builtins"),
  listFirmware: () => ipcRenderer.invoke("list-firmware"),
  importFirmware: () => ipcRenderer.invoke("import-firmware"),
  start: (key) => ipcRenderer.invoke("start", key),
  stop: () => ipcRenderer.invoke("stop"),
  reset: () => ipcRenderer.invoke("reset"),
  touch: (point) => ipcRenderer.invoke("touch", point),
  saveScreen: (url) => ipcRenderer.invoke("save-screen", url),
  openFolder: () => ipcRenderer.invoke("open-folder"),
  onStatus: (fn) => ipcRenderer.on("status", (_, value) => fn(value)),
  onFrame: (fn) => ipcRenderer.on("frame", (_, value) => fn(value)),
  onLogs: (fn) => ipcRenderer.on("logs", (_, value) => fn(value)),
  onFrameError: (fn) => ipcRenderer.on("frame-error", (_, value) => fn(value)),
});
