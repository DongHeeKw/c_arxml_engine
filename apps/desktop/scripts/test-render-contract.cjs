// Real engine output → actual React component → static markup. Not a browser interaction test.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const os = require("node:os");
const { spawnSync } = require("node:child_process");
const ts = require("typescript");
const React = require("react");
const { renderToStaticMarkup } = require("react-dom/server");
const previous = require.extensions[".tsx"];
require.extensions[".tsx"] = (module, filename) => {
  const compiled = ts.transpileModule(fs.readFileSync(filename,"utf8"), {
    compilerOptions: { jsx: ts.JsxEmit.ReactJSX, module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2022 },
  });
  module._compile(compiled.outputText, filename);
};
const { NetworkWorkspace } = require("../src/NetworkWorkspace.tsx");
if (previous) require.extensions[".tsx"] = previous; else delete require.extensions[".tsx"];
const root = path.resolve(__dirname,"../../..");
const temporary = fs.mkdtempSync(path.join(os.tmpdir(),"studio-render-contract-"));
try {
  const text='BU_: A B C\nBO_ 1 Multicast: 8 A\n SG_ State : 0|8@1+ (1,0) [0|255] "" B,C\n';
  const requests=[
    ["dbc.import",{ files:[{name:"shared.dbc",sourceUri:path.join(temporary,"shared.dbc"),channel:"CAN1",frameFormat:"classic",text}] }],
    ["dbc.sources",{}],["network.ecu",{channel:"CAN1",ecu:"B"}],["network.ecu",{channel:"CAN1",ecu:"A"}],
  ];
  const processResult=spawnSync(process.env.STUDIO_ENGINE_PATH || path.join(root,"build/ui-verification/studio-engine.exe"),[],{
    env:{...process.env,STUDIO_DATABASE_PATH:path.join(temporary,"state.sqlite3")},windowsHide:true,encoding:"utf8",timeout:15000,
    input:requests.map(([method,params],index)=>JSON.stringify({jsonrpc:"2.0",id:index+1,method,params})).join("\n")+"\n",
  });
  assert.ifError(processResult.error);assert.equal(processResult.status,0,processResult.stderr);
  const results=processResult.stdout.trim().split(/\r?\n/).map((line)=>JSON.parse(line));
  assert.ok(results.every((response)=>!response.error));assert.equal(results[0].result.accepted,true);
  const props={sources:results[1].result.files,channel:"CAN1",ecu:"B",view:results[2].result,error:null,onChannel(){},onEcu(){},onImport(){}};
  const receive=renderToStaticMarkup(React.createElement(NetworkWorkspace,props));
  assert.match(receive,/aria-label="Show messages with A"/);
  assert.doesNotMatch(receive,/aria-label="Show messages with C"/);
  assert.match(receive,/aria-label="Inspect Multicast"/);
  assert.match(receive,/aria-label="Message inspector"/);
  assert.match(receive,/State/);
  assert.match(receive,/Not live bus traffic/);
  const transmit=renderToStaticMarkup(React.createElement(NetworkWorkspace,{...props,ecu:"A",view:results[3].result}));
  assert.match(transmit,/aria-label="Show messages with B"/);
  assert.match(transmit,/aria-label="Show messages with C"/);
  const empty=renderToStaticMarkup(React.createElement(NetworkWorkspace,{...props,sources:[],channel:"",ecu:"",view:null}));
  assert.match(empty,/Your network starts here/);assert.doesNotMatch(empty,/class="peer-node/);
  const error=renderToStaticMarkup(React.createElement(NetworkWorkspace,{...props,view:null,error:'<img src=x onerror="alert(1)">'}));
  assert.match(error,/role="alert"/);assert.doesNotMatch(error,/<img/);assert.match(error,/&lt;img/);
  const css=fs.readFileSync(path.join(__dirname,"../src/workspace.css"),"utf8");
  const token=(name)=>css.match(new RegExp(`--${name}: (#[0-9a-f]{6})`))[1];
  const luminance=(hex)=>{ const c=hex.slice(1).match(/../g).map((part)=>parseInt(part,16)/255).map((v)=>v<=0.04045?v/12.92:((v+0.055)/1.055)**2.4);return c[0]*0.2126+c[1]*0.7152+c[2]*0.0722; };
  for(const [fg,bg] of [["text-secondary","surface"],["text-secondary","canvas"],["selected-text","selected-surface"]]) {
    const a=luminance(token(fg)),b=luminance(token(bg));assert.ok((Math.max(a,b)+0.05)/(Math.min(a,b)+0.05)>=4.5,`${fg}/${bg} text contrast`);
  }
  const folder=path.join(root,"build/ui-evidence/static");fs.mkdirSync(folder,{recursive:true});
  fs.writeFileSync(path.join(folder,"render-contract-results.json"),JSON.stringify({
    status:"PASS",scope:"C++ SQLite output and React static rendering; no DOM interaction or visual qualification",
    checks:["Receiver has transmitter peer only, not multicast sibling", "Transmitter has both receiver peers", "Messages and source signals reach inspector", "Empty state invents no ECUs", "Diagnostic text is escaped", "Motion is labelled as configuration topology", "Three primary secondary/selected text token pairs meet 4.5:1 contrast"],
  },null,2));
  console.log("PASS: 7 rendering/contrast contracts (static; browser interaction not covered).");
} finally { fs.rmSync(temporary,{recursive:true,force:true,maxRetries:5,retryDelay:100}); }
