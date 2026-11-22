// Electron preload contract test: no GUI, no Electron runtime, no npm install.
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
test('preload exposes only list and set via IPC',async()=>{
 const api={};const sent=[];
 const contextBridge={exposeInMainWorld:(name,value)=>{assert.equal(name,'ecu');api[name]=value;}};
 const ipcRenderer={invoke:async(...args)=>{sent.push(args);return [];}};
 const code=fs.readFileSync(path.resolve(__dirname,'../electron/preload.cjs'),'utf8');
 vm.runInNewContext(code,{require:(name)=>{assert.equal(name,'electron');return {contextBridge,ipcRenderer};}});
 assert.deepEqual(Object.keys(api.ecu).sort(),['list','set']);
 await api.ecu.list();
 await api.ecu.set('ADAS',false);
 assert.equal(JSON.stringify(sent),JSON.stringify([['ecu:list'],['ecu:set','ADAS',false]]));
 assert.equal('exec' in api.ecu,false);
});
