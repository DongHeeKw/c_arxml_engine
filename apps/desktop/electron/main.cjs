const { app, BrowserWindow, ipcMain } = require("electron");
const { spawn } = require("node:child_process");
const path = require("node:path");
const { readFile, stat, realpath } = require("node:fs/promises");
const { dialog } = require("electron");

const MAX_RESPONSE_BYTES = 1024 * 1024;
const MAX_DBC_FILE_BYTES = 900 * 1024;
const REQUEST_TIMEOUT_MS = 15_000;

let mainWindow;
let engine;
let responseBuffer = "";
let nextRequestId = 1;
const pendingRequests = new Map();

function engineExecutablePath() {
  if (process.env.STUDIO_ENGINE_PATH) {
    return process.env.STUDIO_ENGINE_PATH;
  }
  const executableName = process.platform === "win32" ? "studio-engine.exe" : "studio-engine";
  const root = path.resolve(app.getAppPath(), "../..");
  return path.join(root, "build", "engine", executableName);
}

function rejectPendingRequests(message) {
  for (const { reject, timeout } of pendingRequests.values()) {
    clearTimeout(timeout);
    reject(new Error(message));
  }
  pendingRequests.clear();
}

function handleEngineLine(line) {
  let response;
  try {
    response = JSON.parse(line);
  } catch (error) {
    console.error("Engine emitted invalid JSON:", error);
    return;
  }

  const pending = pendingRequests.get(response.id);
  if (!pending) {
    return;
  }
  pendingRequests.delete(response.id);
  clearTimeout(pending.timeout);
  if (response.error) {
    pending.reject(new Error(response.error.message || "Engine request failed"));
  } else {
    pending.resolve(response.result);
  }
}

function startEngine() {
  engine = spawn(engineExecutablePath(), [], {
    env: {
      ...process.env,
      STUDIO_DATABASE_PATH: process.env.STUDIO_DATABASE_PATH ||
        path.join(app.getPath("userData"), "studio.sqlite3"),
    },
    stdio: ["pipe", "pipe", "pipe"],
    windowsHide: true,
  });

  engine.stdout.setEncoding("utf8");
  engine.stdout.on("data", (chunk) => {
    responseBuffer += chunk;
    let newlineIndex = responseBuffer.indexOf("\n");
    while (newlineIndex !== -1) {
      const line = responseBuffer.slice(0, newlineIndex);
      responseBuffer = responseBuffer.slice(newlineIndex + 1);
      if (Buffer.byteLength(line, "utf8") > MAX_RESPONSE_BYTES) {
        rejectPendingRequests("Engine response exceeded the 1 MiB limit");
        engine.kill();
        return;
      }
      if (line.length > 0) handleEngineLine(line);
      newlineIndex = responseBuffer.indexOf("\n");
    }
    if (Buffer.byteLength(responseBuffer, "utf8") > MAX_RESPONSE_BYTES) {
      rejectPendingRequests("Engine response exceeded the 1 MiB limit");
      engine.kill();
      responseBuffer = "";
    }
  });
  engine.stderr.setEncoding("utf8");
  engine.stderr.on("data", (chunk) => console.error(`[engine] ${chunk.trimEnd()}`));
  engine.on("error", (error) => {
    rejectPendingRequests(`Could not start the C++ engine: ${error.message}`);
  });
  engine.on("close", (code) => {
    rejectPendingRequests(`C++ engine exited${code === null ? "" : ` with code ${code}`}`);
  });
}

function requestEngine(method, params = {}) {
  if (!engine || engine.killed || !engine.stdin.writable) {
    return Promise.reject(new Error("C++ engine is not available"));
  }

  const id = nextRequestId++;
  const payload = `${JSON.stringify({ jsonrpc: "2.0", id, method, params })}\n`;
  if (Buffer.byteLength(payload, "utf8") > MAX_RESPONSE_BYTES) {
    return Promise.reject(new Error("Engine request exceeds the 1 MiB limit"));
  }

  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      pendingRequests.delete(id);
      reject(new Error(`Engine request timed out: ${method}`));
    }, REQUEST_TIMEOUT_MS);
    pendingRequests.set(id, { resolve, reject, timeout });
    engine.stdin.write(payload, "utf8", (error) => {
      if (!error) return;
      const pending = pendingRequests.get(id);
      if (pending) {
        pendingRequests.delete(id);
        clearTimeout(pending.timeout);
        pending.reject(error);
      }
    });
  });
}

function createWindow() {
  mainWindow = new BrowserWindow({
    show: process.env.STUDIO_HEADLESS !== "1",
    width: 1440,
    height: 900,
    minWidth: 1000,
    minHeight: 650,
    webPreferences: {
      preload: path.join(__dirname, "preload.cjs"),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });

  if (process.env.VITE_DEV_SERVER_URL) {
    void mainWindow.loadURL(process.env.VITE_DEV_SERVER_URL);
  } else {
    void mainWindow.loadFile(path.join(__dirname, "../dist/index.html"));
  }
}

ipcMain.handle("engine:request", (event, method, params) => {
  if (!mainWindow || event.sender !== mainWindow.webContents) throw new Error("Invalid application window");
  if (typeof method !== "string" || !/^[a-zA-Z][a-zA-Z0-9.]*$/.test(method)) {
    throw new Error("Invalid engine method");
  }
  if (params !== undefined && (params === null || typeof params !== "object" || Array.isArray(params))) {
    throw new Error("Engine params must be an object");
  }
  return requestEngine(method, params || {});
});

ipcMain.handle("files:openDbc", async (event) => {
  if (!mainWindow || event.sender !== mainWindow.webContents) {
    throw new Error("DBC file selection is only available to the application window");
  }
  const selection = await dialog.showOpenDialog(mainWindow, {
    title: "Import DBC files",
    properties: ["openFile", "multiSelections"],
    filters: [{ name: "CAN database files", extensions: ["dbc"] }],
  });
  if (selection.canceled) {
    return [];
  }

  const files = [];
  for (const filePath of selection.filePaths) {
    const fileInfo = await stat(filePath);
    if (fileInfo.size > MAX_DBC_FILE_BYTES) {
      throw new Error(`${path.basename(filePath)} exceeds the 900 KiB import preview limit`);
    }
    files.push({
      name: path.basename(filePath),
      sourceUri: await realpath(filePath),
      text: await readFile(filePath, "utf8"),
    });
  }
  return files;
});

ipcMain.handle("project:inspect", async (event) => {
  if (!mainWindow || event.sender !== mainWindow.webContents) throw new Error("Invalid application window");
  const selection = await dialog.showOpenDialog(mainWindow, {
    title: "Inspect Eclipse project", properties: ["openDirectory"],
  });
  if (selection.canceled) return null;
  return requestEngine("project.inspect", { root: await realpath(selection.filePaths[0]) });
});

app.whenReady().then(() => {
  startEngine();
  createWindow();
  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on("before-quit", () => {
  if (engine && !engine.killed) engine.kill();
  rejectPendingRequests("Application is shutting down");
});

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") app.quit();
});
