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
  {
    jsonrpc: "2.0",
    id: 4,
    method: "dbc.parse",
    params: {
      text: [
        "VERSION \"1.0\"",
        "BU_: ECU_A ECU_B",
        "BO_ 291 Status: 8 ECU_A",
        " SG_ Speed : 0|16@1+ (0.1,0) [0|250] \"km/h\" ECU_B",
      ].join("\n"),
    },
  },
  {
    jsonrpc: "2.0",
    id: 5,
    method: "dbc.parse",
    params: {
      text: [
        "BU_: ECU_A",
        "BO_ 1 Frame: 1 ECU_A",
        " SG_ Outside : 7|2@1+ (1,0) [0|3] \"\" ECU_A",
      ].join("\n"),
    },
  },
  {
    jsonrpc: "2.0",
    id: 6,
    method: "dbc.parse",
    params: {
      text: [
        "BU_: ECU_A",
        "BO_ 2 TwoBytes: 2 ECU_A",
        " SG_ Motorola : 7|16@0+ (1,0) [0|1] \"\" ECU_A",
      ].join("\n"),
    },
  },
  { jsonrpc: "2.0", id: 7, method: "dbc.parse", params: { text: "BO_ broken" } },
  { jsonrpc: "2.0", id: 8, method: "dbc.parse", params: { text: 42 } },
];
const input = `${requests.map((request) => JSON.stringify(request)).join("\n")}\n{invalid\n`;
const processResult = spawnSync(executable, [], { input, encoding: "utf8" });

assert.equal(processResult.error, undefined, processResult.error?.message);
assert.equal(processResult.status, 0, processResult.stderr);
const responses = processResult.stdout.trim().split(/\r?\n/).map((line) => JSON.parse(line));
assert.equal(responses.length, 9);
assert.equal(responses[0].result.engineVersion, "0.1.0");
assert.equal(responses[0].result.protocolVersion, "1.0");
assert.equal(responses[1].result.projectOpen, false);
assert.equal(responses[2].id, "한글😀");
assert.equal(responses[2].error.code, -32601);
assert.deepEqual(responses[3].result.nodes, ["ECU_A", "ECU_B"]);
assert.equal(responses[3].result.messages[0].frameId, 291);
assert.equal(responses[3].result.messages[0].signals[0].byteOrder, "little-endian");
assert.deepEqual(responses[3].result.messages[0].signals[0].receivers, ["ECU_B"]);
assert.equal(responses[3].result.issues.length, 0);
assert.equal(responses[4].result.issues[0].code, "DBC_SIGNAL_OUT_OF_FRAME");
assert.equal(responses[5].result.issues.length, 0);
assert.equal(responses[6].result.issues[0].code, "DBC_INVALID_MESSAGE_DECLARATION");
assert.equal(responses[7].error.code, -32602);
assert.equal(responses[8].error.code, -32700);

console.log("Engine JSON-RPC smoke tests passed.");
