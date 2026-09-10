// electron/preload.cjs — the safe bridge between the web UI (renderer) and the
// native shell (main). The desktop app exposes a MoonDay namespace and keeps the
// legacy JESTER alias for compatibility with older renderer code.

const { contextBridge, ipcRenderer } = require("electron");

const moonDay = {
  isElectron: true,
  enterMainframe: () => ipcRenderer.invoke("mainframe:set", true),
  exitMainframe: () => ipcRenderer.invoke("mainframe:set", false),
  launchApp: (name) => ipcRenderer.invoke("os:launch", name),
  closeApp: (name) => ipcRenderer.invoke("os:close", name),
  hideApp: (name) => ipcRenderer.invoke("os:hideApp", name),
  webSearch: (query, engine) => ipcRenderer.invoke("os:search", query, engine),
  openInBrowser: (url) => ipcRenderer.invoke("os:open", url),
  media: (action) => ipcRenderer.invoke("os:media", action),
  systemCommand: (command, arg) => ipcRenderer.invoke("os:command", command, arg),
  readDirectory: (path) => ipcRenderer.invoke("system:readDirectory", path),
  openFile: (path) => ipcRenderer.invoke("system:openFile", path),
  openFolder: (path) => ipcRenderer.invoke("system:openFolder", path),
  getSystemInfo: () => ipcRenderer.invoke("system:getSystemInfo"),
  getBatteryInfo: () => ipcRenderer.invoke("system:getBatteryInfo"),
  getNetworkInfo: () => ipcRenderer.invoke("system:getNetworkInfo"),
  getCPUInfo: () => ipcRenderer.invoke("system:getCPUInfo"),
  getMemoryInfo: () => ipcRenderer.invoke("system:getMemoryInfo"),
  getStorageInfo: () => ipcRenderer.invoke("system:getStorageInfo"),
  setVolume: (value) => ipcRenderer.invoke("system:setVolume", value),
  getVolume: () => ipcRenderer.invoke("system:getVolume"),
  takeScreenshot: (path) => ipcRenderer.invoke("system:takeScreenshot", path),
  getClipboard: () => ipcRenderer.invoke("system:getClipboard"),
  setClipboard: (value) => ipcRenderer.invoke("system:setClipboard", value),
  showNotification: (title, body) => ipcRenderer.invoke("system:showNotification", title, body),
  openURL: (url) => ipcRenderer.invoke("system:openURL", url),
  onVoiceListen: (cb) => ipcRenderer.on("voice:listen", () => cb()),
  onEnterMainframe: (cb) => ipcRenderer.on("mainframe:enter", () => cb()),
  onExitMainframe: (cb) => ipcRenderer.on("mainframe:exit", () => cb()),
  onTechStack: (cb) => ipcRenderer.on("techstack", () => cb()),
  system: {
    launchApplication: (name) => ipcRenderer.invoke("system:launchApplication", name),
    openFile: (path) => ipcRenderer.invoke("system:openFile", path),
    openFolder: (path) => ipcRenderer.invoke("system:openFolder", path),
    readDirectory: (path) => ipcRenderer.invoke("system:readDirectory", path),
    getSystemInfo: () => ipcRenderer.invoke("system:getSystemInfo"),
    getBatteryInfo: () => ipcRenderer.invoke("system:getBatteryInfo"),
    getNetworkInfo: () => ipcRenderer.invoke("system:getNetworkInfo"),
    getCPUInfo: () => ipcRenderer.invoke("system:getCPUInfo"),
    getMemoryInfo: () => ipcRenderer.invoke("system:getMemoryInfo"),
    getStorageInfo: () => ipcRenderer.invoke("system:getStorageInfo"),
    setVolume: (value) => ipcRenderer.invoke("system:setVolume", value),
    getVolume: () => ipcRenderer.invoke("system:getVolume"),
    takeScreenshot: (path) => ipcRenderer.invoke("system:takeScreenshot", path),
    getClipboard: () => ipcRenderer.invoke("system:getClipboard"),
    setClipboard: (value) => ipcRenderer.invoke("system:setClipboard", value),
    showNotification: (title, body) => ipcRenderer.invoke("system:showNotification", title, body),
    openURL: (url) => ipcRenderer.invoke("system:openURL", url),
  },
};

contextBridge.exposeInMainWorld("moonDay", moonDay);
contextBridge.exposeInMainWorld("jester", moonDay);
