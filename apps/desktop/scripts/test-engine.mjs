import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import path from "node:path";

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const executableName = process.platform === "win32" ? "studio-engine.exe" : "studio-engine";
const executable = process.env.STUDIO_ENGINE_PATH ||
  path.resolve(scriptDirectory, "../../../build/engine", executableName);
const requests = [
  { jsonrpc: "2.0", id: 1, method: "initialize" },
  { jsonrpc: "2.0", id: 2, method: "project.status", params: {} },
  { jsonrpc: "2.0", id: "한글😀", method: "missing" },
];
const input = `${requests.map((request) => JSON.stringify(request)).join("\n")}\n{invalid\n`;
const processResult = spawnSync(executable, [], { input, encoding: "utf8" });

assert.equal(processResult.error, undefined, processResult.error?.message);
assert.equal(processResult.status, 0, processResult.stderr);
const responses = processResult.stdout.trim().split(/\r?\n/).map((line) => JSON.parse(line));
assert.equal(responses.length, 4);
assert.equal(responses[0].result.engineVersion, "0.1.0");
assert.equal(responses[0].result.protocolVersion, "1.0");
assert.equal(responses[1].result.projectOpen, false);
assert.equal(responses[2].id, "한글😀");
assert.equal(responses[2].error.code, -32601);
assert.equal(responses[3].error.code, -32700);

console.log("Engine JSON-RPC smoke tests passed.");
