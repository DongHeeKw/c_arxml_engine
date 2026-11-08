import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import path from "node:path";

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const executableName = process.platform === "win32" ? "studio-engine.exe" : "studio-engine";
const executable = process.env.STUDIO_ENGINE_PATH ||
  path.resolve(scriptDirectory, "../../../build/engine", executableName);
const fixturePath = path.resolve(
  scriptDirectory,
  "../../../fixtures/synthetic/eclipse_can_project/VehicleNetwork.dbc",
);
const fixtureText = await readFile(fixturePath, "utf8");
const changedFixtureText = fixtureText
  .replace("BO_ 291 VehicleStatus:", "BO_ 801 VehicleStatus:")
  .replace('(0.1,0) [0|250] "km/h"', '(0.2,0) [0|250] "km/h"');
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
  { jsonrpc: "2.0", id: 9, method: "dbc.parse", params: { text: fixtureText } },
  {
    jsonrpc: "2.0",
    id: 10,
    method: "dbc.parseBatch",
    params: {
      files: [
        { name: "VehicleNetwork.dbc", text: fixtureText },
        { name: "Small.dbc", text: "BO_ 1 Frame: 1 ECU_A" },
      ],
    },
  },
  {
    jsonrpc: "2.0",
    id: 11,
    method: "project.configure",
    params: {
      name: "InvalidProject",
      mcu: { vendor: "Vendor", family: "Family", partNumber: "Part" },
      tasks: [{ name: "NoPeriod", trigger: "periodic" }],
    },
  },
  {
    jsonrpc: "2.0",
    id: 12,
    method: "project.configure",
    params: {
      name: "MissingServiceId",
      mcu: { partNumber: "Part" },
      dcmServices: [{ name: "ServiceWithoutId", enabled: true }],
    },
  },
  {
    jsonrpc: "2.0",
    id: 13,
    method: "project.configure",
    params: {
      name: "BodyController",
      mcu: { vendor: "Infineon", family: "AURIX", partNumber: "TC397" },
      dbcSources: ["VehicleNetwork.dbc"],
      tasks: [
        { name: "Com_MainFunction", trigger: "periodic", periodMs: 5 },
        { name: "CanRx", trigger: "event", event: "CanIfRxIndication" },
        { name: "Can0_Isr", trigger: "isr", isrVector: "CAN0_IRQHandler" },
      ],
      dcmServices: [{ serviceId: 34, name: "ReadDataByIdentifier", enabled: true }],
      requirements: [{ id: "SRS-COM-001", text: "Transmit configured CAN signals every 5 ms." }],
    },
  },
  {
    jsonrpc: "2.0",
    id: 14,
    method: "project.configure",
    params: {
      name: "RejectedUpdate",
      mcu: { partNumber: "OtherPart" },
      tasks: [{ name: "MissingPeriod", trigger: "periodic" }],
    },
  },
  { jsonrpc: "2.0", id: 15, method: "project.status", params: {} },
  { jsonrpc: "2.0", id: 16, method: "dbc.parse", params: { text: fixtureText } },
  {
    jsonrpc: "2.0",
    id: 17,
    method: "dbc.diff",
    params: { beforeText: fixtureText, afterText: changedFixtureText },
  },
];
const input = `${requests.map((request) => JSON.stringify(request)).join("\n")}\n{invalid\n`;
const processResult = spawnSync(executable, [], { input, encoding: "utf8" });

assert.equal(processResult.error, undefined, processResult.error?.message);
assert.equal(processResult.status, 0, processResult.stderr);
const responses = processResult.stdout.trim().split(/\r?\n/).map((line) => JSON.parse(line));
assert.equal(responses.length, 18);
assert.equal(responses[0].result.engineVersion, "0.1.0");
assert.equal(responses[0].result.protocolVersion, "1.0");
assert.ok(responses[0].result.supportedMethods.includes("dbc.parseBatch"));
assert.ok(responses[0].result.supportedMethods.includes("project.configure"));
assert.ok(responses[0].result.supportedMethods.includes("dbc.diff"));
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
assert.deepEqual(responses[8].result.nodes, ["ECU_A", "ECU_B"]);
assert.equal(responses[8].result.messages.length, 2);
assert.equal(responses[8].result.messages[0].name, "VehicleStatus");
assert.equal(responses[8].result.issues.length, 0);
assert.equal(responses[9].result.length, 2);
assert.equal(responses[9].result[0].name, "VehicleNetwork.dbc");
assert.equal(responses[9].result[0].preview.messages.length, 2);
assert.equal(responses[10].result.accepted, false);
assert.ok(responses[10].result.issues.some((issue) => issue.code === "SRS_TASK_PERIOD_REQUIRED"));
assert.equal(responses[11].error.code, -32602);
assert.equal(responses[12].result.accepted, true);
assert.equal(responses[12].result.project.tasks.length, 3);
assert.equal(responses[12].result.project.dcmServices[0].serviceId, 34);
assert.equal(responses[13].result.accepted, false);
assert.equal(responses[14].result.projectOpen, true);
assert.equal(responses[14].result.revision, 1);
assert.equal(responses[14].result.persistence, "in-memory-draft");
assert.equal(responses[14].result.definition.name, "BodyController");
assert.deepEqual(responses[15].result, responses[8].result);
assert.equal(responses[16].result.changes.messages[0].kind, "modified");
assert.equal(responses[16].result.changes.messages[0].matchBasis, "name");
assert.equal(responses[16].result.changes.messages[0].previousFrameId, 291);
assert.equal(responses[16].result.changes.messages[0].currentFrameId, 801);
assert.ok(responses[16].result.changes.messages[0].signals.some((signal) =>
  signal.name === "VehicleSpeed" &&
  signal.kind === "modified" &&
  signal.changedFields.includes("factor")));
assert.equal(responses[16].result.changes.messages[1].kind, "unchanged");
assert.deepEqual(responses[16].result.beforeIssues, []);
assert.deepEqual(responses[16].result.afterIssues, []);
assert.equal(
  responses[16].result.beforeUnsupportedLines,
  responses[16].result.afterUnsupportedLines,
);
assert.equal(responses[17].error.code, -32700);

console.log("Engine JSON-RPC smoke tests passed.");
