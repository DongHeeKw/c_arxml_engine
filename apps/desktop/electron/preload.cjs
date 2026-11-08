const { contextBridge, ipcRenderer } = require("electron");

contextBridge.exposeInMainWorld("studio", {
  request: (method, params) => ipcRenderer.invoke("engine:request", method, params),
});
