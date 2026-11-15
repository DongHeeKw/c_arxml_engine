const {contextBridge,ipcRenderer}=require('electron');
contextBridge.exposeInMainWorld('ecu',{list:()=>ipcRenderer.invoke('ecu:list'),set:(name,enabled)=>ipcRenderer.invoke('ecu:set',name,enabled)});
