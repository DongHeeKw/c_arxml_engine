const {app,BrowserWindow,ipcMain}=require('electron');
const {execFile}=require('node:child_process');
const {promisify}=require('node:util');
const path=require('node:path');
const run=promisify(execFile);
const root=path.resolve(__dirname,'..');
function enginePath(){return process.env.ECU_ENGINE || path.join(root,'build',process.platform==='win32'?'ecu_engine.exe':'ecu_engine');}
async function engine(...args){
  console.log('[Electron main] IPC -> C++',args);
  const {stdout,stderr}=await run(enginePath(),[path.join(app.getPath('userData'),'ecu.sqlite'),...args],{timeout:10000});
  console.log(stderr.trim()); console.log('[Electron main] C++ -> IPC',stdout.trim()); return JSON.parse(stdout);
}
app.whenReady().then(()=>{
  ipcMain.handle('ecu:list',()=>engine('list'));
  ipcMain.handle('ecu:set',(_,name,enabled)=>{
    if(!['ADAS','BCM','CGW','VCU'].includes(name)||typeof enabled!=='boolean')throw Error('Invalid ECU request');
    return engine('set',name,enabled?'1':'0');
  });
  const win=new BrowserWindow({width:1050,height:750,webPreferences:{preload:path.join(__dirname,'preload.cjs'),contextIsolation:true,nodeIntegration:false,sandbox:true}});
  win.loadFile(path.join(root,'dist','index.html'));
  if(process.env.DEMO_DEBUG==='1')win.webContents.openDevTools();
});
app.on('window-all-closed',()=>app.quit());
