import React, { useEffect, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { Vehicle } from '../three/Vehicle';
export type Ecu = {name:string; enabled:boolean};
declare global { interface Window { ecu: {list():Promise<Ecu[]>;set(name:string,enabled:boolean):Promise<Ecu[]>} } }
const info:Record<string,{mcu:string;network:string;modules:string[]}>={
 ADAS:{mcu:'TC397',network:'CAN FD',modules:['CanIf','PduR','Com','Dcm']},
 BCM:{mcu:'TC375',network:'CAN FD',modules:['CanIf','PduR','Com','Dcm','Dem']},
 CGW:{mcu:'TC397',network:'CAN FD / Ethernet',modules:['CanIf','PduR','SoAd','Dcm']},
 VCU:{mcu:'TC387',network:'CAN FD',modules:['CanIf','PduR','Com','Dcm']}
};
function App(){
 const [ecus,setEcus]=useState<Ecu[]>([]);
 const [selected,setSelected]=useState('ADAS');
 const [busy,setBusy]=useState(false);
 const [error,setError]=useState('');
 const [logs,setLogs]=useState<string[]>([]);
 const log=(s:string)=>{console.log(s);setLogs(old=>[...old,s].slice(-30));};
 async function refresh(){setBusy(true);setError('');try{log('[React] ecu:list -> IPC');const rows=await window.ecu.list();setEcus(rows);log('[React] response: '+JSON.stringify(rows));}catch(e){setError(String(e));}finally{setBusy(false);}}
 useEffect(()=>{void refresh();},[]);
 async function change(name:string,enabled:boolean){setBusy(true);setError('');try{log('[React] ecu:set '+name+' '+enabled);const rows=await window.ecu.set(name,enabled);setEcus(rows);log('[React] committed response: '+JSON.stringify(rows));}catch(e){setError(String(e));}finally{setBusy(false);}}
 const ecu=ecus.find(e=>e.name===selected),meta=info[selected];
 return <div className="studio"><style>{`
 *{box-sizing:border-box}body{font:14px system-ui,sans-serif;background:#10151e;color:#dce5f2}button{font:inherit;color:inherit;background:#283345;border:1px solid #45536a;border-radius:5px;padding:8px 12px;cursor:pointer}button:disabled{opacity:.4;cursor:default}.studio{height:100vh;display:grid;grid-template-rows:42px 48px minmax(0,1fr) 125px 30px;overflow:hidden}.title,.toolbar,.status{display:flex;align-items:center;gap:10px;padding:0 16px;border-bottom:1px solid #344154;background:#192331}.title{font-weight:650}.toolbar small{margin-left:auto;color:#9caec5}.workspace{display:grid;grid-template-columns:205px minmax(240px,1fr) 245px;min-height:0}.panel{padding:16px;background:#202a38;overflow:auto}.panel h3{margin:0 0 20px;font-size:12px;text-transform:uppercase;letter-spacing:1px;color:#a8b8cc}.tree{display:block;text-align:left;width:100%;margin:5px 0;padding:9px;background:transparent;border:1px solid transparent}.tree.active{background:#32465d;border-color:#527391}.dot{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:9px;background:#64748b}.dot.on{background:#38d9a9}.canvas{position:relative;min-height:0;background:#101827;overflow:hidden}.canvas-head{position:absolute;top:10px;left:15px;right:15px;z-index:1;pointer-events:none;display:flex;justify-content:space-between;color:#9fb0c6;font-size:12px}.field{margin:18px 0}.field label{display:block;color:#9fb0c6;font-size:12px;margin-bottom:5px}.module{padding:6px 0;border-bottom:1px solid #384456}.console{padding:10px 16px;overflow:auto;background:#10151d;border-top:1px solid #344154;font:12px monospace;color:#9edcc9}.console b{display:block;color:#9caec5;margin-bottom:6px}.status{font-size:12px;justify-content:space-between}.error{color:#ffb3ad;overflow-wrap:anywhere}.legend{font-size:11px;color:#9caec5;margin-top:18px;line-height:1.8}input{accent-color:#38d9a9}
 `}</style>
 <header className="title">◈ ARXML Configuration Studio <span style={{marginLeft:'auto',fontSize:12,color:'#9caec5'}}>HELLO WORLD / v0.2</span></header>
 <nav className="toolbar"><button disabled>＋ Import DBC</button><button disabled>Generate Code</button><button disabled={busy} onClick={()=>void refresh()}>↻ Refresh DB</button><small>{busy?'Processing…':'Configuration workspace'}</small></nav>
 <main className="workspace">
 <aside className="panel"><h3>Project Explorer</h3><div>▾ Vehicle</div><div style={{paddingLeft:10,margin:'12px 0'}}>▾ Network / ECUs</div>{ecus.map(e=><button key={e.name} className={'tree '+(selected===e.name?'active':'')} onClick={()=>{setSelected(e.name);log('[React] selected '+e.name);}}><span className={'dot '+(e.enabled?'on':'')}/>▣ {e.name}</button>)}<div className="legend">● Green: enabled<br/>● Gray: disabled<br/>Yellow outline: selected</div></aside>
 <section className="canvas"><div className="canvas-head"><span>VEHICLE TOPOLOGY · 3D</span><span>Click ECU to select</span></div><Vehicle ecus={ecus} selected={selected} onSelect={name=>{setSelected(name);log('[Three.js] selected '+name);}}/></section>
 <aside className="panel"><h3>Properties</h3><h2>{selected}</h2><div className="field"><label>Controller</label>{meta?.mcu}</div><div className="field"><label>Network</label>{meta?.network}</div><div className="field"><label>Status</label><input id="enabled" type="checkbox" checked={ecu?.enabled??false} disabled={busy||!ecu} onChange={e=>void change(selected,e.target.checked)}/><label htmlFor="enabled" style={{display:'inline',color:'inherit'}}> Enabled</label></div><div className="field"><label>BSW Modules</label>{meta?.modules.map(m=><div className="module" key={m}>▤ {m}</div>)}</div>{error&&<div role="alert" className="error">{error}</div>}<div className="legend">MCU / Network / Modules: UI sample metadata.<br/>Enabled: real SQLite state.</div></aside>
 </main><section className="console"><b>OUTPUT / LAYER TRACE</b>{logs.map((l,i)=><div key={i}>{l}</div>)}{error&&<div className="error">{error}</div>}</section><footer className="status"><span>{ecus.filter(e=>e.enabled).length}/{ecus.length} ECU enabled</span><span>React · Three.js · IPC · C++ · SQLite</span></footer></div>;
}
createRoot(document.getElementById('root')!).render(<App/>);
