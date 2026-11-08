const { contextBridge, ipcRenderer } = require("electron");

contextBridge.exposeInMainWorld("studio", {
  openDbcFiles: () => ipcRenderer.invoke("files:openDbc"),
  request: (method, params) => ipcRenderer.invoke("engine:request", method, params),
});
