import { spawnSync } from "node:child_process";
import { mkdtempSync, mkdirSync, writeFileSync, readFileSync, rmSync } from "node:fs";
import { createRequire } from "node:module";
import { createHash } from "node:crypto";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
const require = createRequire(import.meta.url);
const desktop = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const root = path.resolve(desktop, "../..");
const temporary = mkdtempSync(path.join(os.tmpdir(), "studio-ui-"));
const evidence = path.join(root, "build/ui-evidence");
mkdirSync(evidence, { recursive: true });
const source = path.join(temporary, "BodyNetwork.dbc");
writeFileSync(source, 'BU_: BDC GATEWAY CLUSTER HVAC\nBO_ 256 GatewayState: 8 GATEWAY\n SG_ VehicleSpeed : 0|16@1+ (0.1,0) [0|250] "km/h" BDC,CLUSTER\nBO_ 291 DoorStatus: 8 BDC\n SG_ DoorOpen : 0|1@1+ (1,0) [0|1] "" CLUSTER\nBO_ 292 CabinTemperature: 8 HVAC\n SG_ Temperature : 0|8@1+ (1,-40) [-40|120] "C" BDC\nCM_ BO_ 291 "Synthetic fixture";\n');
const hash = (file) => createHash("sha256").update(readFileSync(file)).digest("hex");
const before = hash(source);
const env = { ...process.env, STUDIO_HEADLESS: "1", STUDIO_DATABASE_PATH: path.join(temporary, "studio.sqlite3"),
  STUDIO_ENGINE_PATH: process.env.STUDIO_ENGINE_PATH || path.join(root, "build/ui-verification/studio-engine.exe"),
  STUDIO_UI_FIXTURE: source, STUDIO_UI_EVIDENCE: evidence, STUDIO_UI_USER_DATA: path.join(temporary, "electron-profile") };
delete env.ELECTRON_RUN_AS_NODE;
delete env.VITE_DEV_SERVER_URL;
try {
  for (const phase of ["import", "restart"]) {
    const result = spawnSync(process.env.STUDIO_ELECTRON_PATH || require("electron"), [path.join(desktop, "scripts/test-ui.cjs")], {
      cwd: desktop, env: { ...env, STUDIO_UI_PHASE: phase }, encoding: "utf8", windowsHide: true,
      timeout: 90000, maxBuffer: 4 * 1024 * 1024,
    });
    process.stdout.write(result.stdout || "");
    if (result.error || result.status !== 0) throw new Error(result.error?.message || result.stderr || `Electron exited ${result.status}`);
  }
  if (hash(source) !== before) throw new Error("UI workflow modified the original DBC source");
  console.log(`Electron UI verification passed. Screenshots and assertions: ${evidence}`);
} finally { rmSync(temporary, { recursive: true, force: true, maxRetries: 10, retryDelay: 100 }); }
