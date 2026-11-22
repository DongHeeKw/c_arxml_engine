const {contextBridge,ipcRenderer}=require('electron');
contextBridge.exposeInMainWorld('ecu',{list:()=>ipcRenderer.invoke('ecu:list'),history:()=>ipcRenderer.invoke('ecu:history'),set:(name,enabled)=>ipcRenderer.invoke('ecu:set',name,enabled)});
