import { spawn } from "node:child_process";
import { setTimeout as delay } from "node:timers/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";

const desktopDirectory = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const nodeModules = path.join(desktopDirectory, "node_modules");
const viteCli = path.join(nodeModules, "vite", "bin", "vite.js");
const electronCli = path.join(nodeModules, "electron", "cli.js");
const vite = spawn(process.execPath, [viteCli, "--host", "127.0.0.1"], {
  cwd: desktopDirectory,
  stdio: "inherit",
});

let electron;
let shuttingDown = false;

function stop() {
  if (shuttingDown) return;
  shuttingDown = true;
  if (electron && !electron.killed) electron.kill();
  if (!vite.killed) vite.kill();
}

process.on("SIGINT", stop);
process.on("SIGTERM", stop);
vite.on("exit", (code) => {
  if (!shuttingDown) {
    console.error(`Vite exited${code === null ? "" : ` with code ${code}`}`);
    stop();
    process.exitCode = code || 1;
  }
});

async function waitForVite() {
  const deadline = Date.now() + 30_000;
  while (Date.now() < deadline) {
    if (vite.exitCode !== null) throw new Error("Vite exited before becoming ready");
    try {
      const response = await fetch("http://127.0.0.1:5173");
      if (response.ok) return;
    } catch {
      await delay(250);
    }
  }
  throw new Error("Timed out waiting for Vite on http://127.0.0.1:5173");
}

try {
  await waitForVite();
  electron = spawn(process.execPath, [electronCli, "."], {
    cwd: desktopDirectory,
    env: { ...process.env, VITE_DEV_SERVER_URL: "http://127.0.0.1:5173" },
    stdio: "inherit",
  });
  electron.on("exit", (code) => {
    if (!shuttingDown) {
      process.exitCode = code || 0;
      stop();
    }
  });
} catch (error) {
  console.error(error.message);
  stop();
  process.exitCode = 1;
}
