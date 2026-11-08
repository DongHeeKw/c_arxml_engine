const { contextBridge, ipcRenderer, webUtils } = require("electron");

contextBridge.exposeInMainWorld("studio", {
  openDbcFiles: () => ipcRenderer.invoke("files:openDbc"),
  pathForFile: (file) => webUtils.getPathForFile(file),
  inspectProject: () => ipcRenderer.invoke("project:inspect"),
  request: (method, params) => ipcRenderer.invoke("engine:request", method, params),
});
