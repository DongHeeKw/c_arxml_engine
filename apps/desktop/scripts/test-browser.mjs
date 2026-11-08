// Renderer integration fallback: real C++/SQLite via a test CDP binding.
// This does NOT qualify Electron preload, native dialog or IPC integration.
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { mkdtemp, mkdir, readFile, writeFile, rm } from "node:fs/promises";
import { createHash } from "node:crypto";
import { createInterface } from "node:readline";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
import { once } from "node:events";
import os from "node:os";
import path from "node:path";
const require = createRequire(import.meta.url);
const scenarios = require("./ui-scenarios.cjs");
const desktop = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const root = path.resolve(desktop, "../..");
const temp = await mkdtemp(path.join(os.tmpdir(), "studio-browser-"));
const evidence = path.join(root, "build/ui-evidence/browser");
await mkdir(evidence, { recursive: true });
const source = path.join(temp, "BodyNetwork.dbc");
const text = 'BU_: BDC GATEWAY CLUSTER HVAC\nBO_ 256 GatewayState: 8 GATEWAY\n SG_ VehicleSpeed : 0|16@1+ (0.1,0) [0|250] "km/h" BDC,CLUSTER\nBO_ 291 DoorStatus: 8 BDC\n SG_ DoorOpen : 0|1@1+ (1,0) [0|1] "" CLUSTER\nBO_ 292 CabinTemperature: 8 HVAC\n SG_ Temperature : 0|8@1+ (1,-40) [-40|120] "C" BDC\nCM_ BO_ 291 "Synthetic fixture";\n';
await writeFile(source, text);
const dist = path.join(desktop, "dist");
const server = createServer(async (req, res) => {
  try {
    const file = path.resolve(dist, "." + (req.url === "/" ? "/index.html" : decodeURIComponent(req.url.split("?")[0])));
    if (!file.startsWith(dist + path.sep)) { res.writeHead(403).end(); return; }
    res.setHeader("Content-Type", ({ ".html": "text/html", ".js": "text/javascript", ".css": "text/css" })[path.extname(file)] || "application/octet-stream");
    res.end(await readFile(file));
  } catch { res.writeHead(404).end(); }
});
await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
const url = `http://127.0.0.1:${server.address().port}`;
const delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
try {
  for (const phase of ["import", "restart"]) {
    const engine = spawn(process.env.STUDIO_ENGINE_PATH || path.join(root, "build/ui-verification/studio-engine.exe"), [], {
      env: { ...process.env, STUDIO_DATABASE_PATH: path.join(temp, "state.sqlite3") }, windowsHide: true, stdio: ["pipe", "pipe", "pipe"],
    });
    let requestId = 0;
    const pendingEngine = new Map();
    createInterface({ input: engine.stdout }).on("line", (line) => {
      const response = JSON.parse(line); const pending = pendingEngine.get(response.id); pendingEngine.delete(response.id);
      if (pending) response.error ? pending.reject(new Error(response.error.message)) : pending.resolve(response.result);
    });
    const rpc = (method, params) => new Promise((resolve, reject) => {
      const id = ++requestId; pendingEngine.set(id, { resolve, reject });
      engine.stdin.write(JSON.stringify({ jsonrpc: "2.0", id, method, params }) + "\n");
    });
    const profile = path.join(temp, `chrome-${phase}`);
    const browser = spawn(process.env.STUDIO_BROWSER_PATH || "C:/Program Files/Google/Chrome/Application/chrome.exe", ["--headless=new", "--disable-gpu", "--remote-debugging-port=0", `--user-data-dir=${profile}`, "--no-first-run", "--no-default-browser-check", "about:blank"], { windowsHide: true, stdio: ["ignore", "ignore", "pipe"] });
    let browserLog = "";
    browser.stderr.on("data", (chunk) => { browserLog = (browserLog + chunk).slice(-12000); });
    let socket;
    try {
      let port;
      for (let attempt = 0; attempt < 150; attempt++) {
        try { port = (await readFile(path.join(profile, "DevToolsActivePort"), "utf8")).split("\n")[0]; break; } catch { await delay(100); }
      }
      if (!port) throw new Error("Browser could not start a sandboxed headless session");
      const tabs = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
      socket = new WebSocket(tabs.find((tab) => tab.type === "page").webSocketDebuggerUrl);
      await once(socket, "open");
      const pending = new Map(); let sequence = 0;
      const cdp = (method, params = {}) => new Promise((resolve, reject) => {
        const id = ++sequence; const timer = setTimeout(() => { pending.delete(id); reject(new Error(`CDP timeout: ${method}`)); }, 20000);
        pending.set(id, { resolve, reject, timer }); socket.send(JSON.stringify({ id, method, params }));
      });
      const errors = [];
      socket.addEventListener("message", async (event) => {
        const message = JSON.parse(event.data);
        if (message.id) { const job = pending.get(message.id); if (!job) return; pending.delete(message.id); clearTimeout(job.timer); message.error ? job.reject(new Error(message.error.message)) : job.resolve(message.result); }
        if (message.method === "Runtime.exceptionThrown") errors.push(message.params.exceptionDetails.text);
        if (message.method === "Runtime.bindingCalled") {
          const call = JSON.parse(message.params.payload); let result, error;
          try { result = call.method === "$open" ? [{ name: "BodyNetwork.dbc", sourceUri: source, text }] : await rpc(call.method, call.params); }
          catch (cause) { error = cause.message; }
          await cdp("Runtime.evaluate", { expression: `window.__studioReply(${JSON.stringify({ id: call.id, result, error })})` });
        }
      });
      await cdp("Page.enable"); await cdp("Runtime.enable");
      await cdp("Runtime.addBinding", { name: "__studioRequest" });
      await cdp("Page.addScriptToEvaluateOnNewDocument", { source: `(() => {
        let id=0; const pending=new Map();
        const request=(method,params={})=>new Promise((resolve,reject)=>{const key=++id;pending.set(key,{resolve,reject});window.__studioRequest(JSON.stringify({id:key,method,params}));});
        window.__studioReply=reply=>{const task=pending.get(reply.id);pending.delete(reply.id);if(task)reply.error?task.reject(Error(reply.error)):task.resolve(reply.result);};
        window.studio={request,openDbcFiles:()=>request('$open'),pathForFile:()=>'',inspectProject:()=>Promise.resolve(null)};
      })();` });
      const wc = {
        on() {},
        async executeJavaScript(expression) { const response = await cdp("Runtime.evaluate", { expression, awaitPromise: true, returnByValue: true, userGesture: true }); if (response.exceptionDetails) throw new Error(response.exceptionDetails.exception?.description || response.exceptionDetails.text); return response.result.value; },
        async capturePage() { const result = await cdp("Page.captureScreenshot", { format: "png" }); return { toPNG: () => Buffer.from(result.data, "base64") }; },
        async sendInputEvent(event) { await cdp("Input.dispatchKeyEvent", { type: event.type === "keyDown" ? "rawKeyDown" : "keyUp", key: "Enter", code: "Enter", windowsVirtualKeyCode: 13 }); },
        debugger: { async attach() {}, sendCommand: cdp, detach() { void cdp("Emulation.setEmulatedMedia", { features: [] }); } },
      };
      const win = { webContents: wc, setContentSize: (width, height) => cdp("Emulation.setDeviceMetricsOverride", { width, height, deviceScaleFactor: 1, mobile: false }) };
      await cdp("Page.navigate", { url });
      const checks = await scenarios(win, { phase, evidence });
      if (errors.length) throw new Error(`Browser errors: ${errors.join(", ")}`);
      await writeFile(path.join(evidence, `${phase}-results.json`), JSON.stringify({ adapter: "Chromium CDP + real C++ engine; Electron IPC not exercised", phase, checks, errors }, null, 2));
      await cdp("Browser.close").catch(() => {});
    } catch (error) {
      await writeFile(path.join(evidence, "browser-startup-error.txt"), `${error.stack}\n${browserLog}`);
      console.error(browserLog);
      throw error;
    } finally {
      socket?.close(); browser.kill(); engine.stdin.end();
      await Promise.race([once(engine, "exit"), delay(3000)]);
    }
  }
  const sourceHash = createHash("sha256").update(await readFile(source)).digest("hex");
  if (sourceHash !== createHash("sha256").update(text).digest("hex")) throw new Error("Original source changed");
  console.log("Browser renderer integration passed; Electron sandbox qualification remains separate.");
} finally {
  server.close(); await delay(500); await rm(temp, { recursive: true, force: true, maxRetries: 10, retryDelay: 200 });
}
