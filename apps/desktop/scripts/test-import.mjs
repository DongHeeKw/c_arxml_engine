import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { cp, mkdtemp, mkdir, readFile, rm, stat, symlink, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repository = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../../..");
const executable = process.env.STUDIO_ENGINE_PATH || path.join(repository, "build/engine",
  process.platform === "win32" ? "studio-engine.exe" : "studio-engine");
const directory = await mkdtemp(path.join(os.tmpdir(), "studio-import-"));
const sourceText = 'BU_: A B\nBO_ 291 Status: 8 A\n SG_ Speed : 0|16@1+ (0.1,0) [0|250] "km/h" B\n';
const digest = (value) => createHash("sha256").update(value).digest("hex");
try {
  await mkdir(path.join(directory, "one"));
  await mkdir(path.join(directory, "two"));
  const a = { sourceUri: path.join(directory, "one/same.dbc"), name: "same.dbc", channel: "CAN1", frameFormat: "classic", text: sourceText };
  const b = { ...a, sourceUri: path.join(directory, "two/same.dbc"), channel: "CAN2" };
  await writeFile(a.sourceUri, sourceText);
  await writeFile(b.sourceUri, sourceText);
  const originalTime = (await stat(a.sourceUri)).mtimeMs;
  function requestSequence(commands) {
    const input = commands.map(([method, params], index) => JSON.stringify({ jsonrpc: "2.0", id: index + 1, method, params })).join("\n") + "\n";
    const result = spawnSync(executable, [], { input, encoding: "utf8", windowsHide: true,
      env: { ...process.env, STUDIO_DATABASE_PATH: path.join(directory, "state.sqlite3") },
      timeout: 30000, maxBuffer: 8 * 1024 * 1024 });
    assert.ifError(result.error);
    assert.equal(result.status, 0, result.stderr);
    const responses = result.stdout.trim().split(/\r?\n/).map((line) => JSON.parse(line));
    assert.equal(responses.length, commands.length);
    return responses;
  }
  const first = requestSequence([
    ["dbc.import", { files: [a, b] }],
    ["dbc.import", { files: [a, b] }],
    ["network.ecu", { channel: "CAN1", ecu: "B" }],
    ["network.ecu", { channel: "CAN1", ecu: "UNKNOWN" }],
    ["dbc.import", { files: [{ ...b, channel: "CAN1" }] }],
    ["dbc.sources", {}],
    ["dbc.import", { files: [{ ...a, sourceUri: "same.dbc" }] }],
    ["dbc.import", { files: [{ ...a, text: "BO_ broken" }] }],
    ["dbc.sources", {}],
    ["project.inspect", { root: path.join(repository, "fixtures/synthetic/eclipse_can_project") }],
    ["project.inspect", { root: "relative" }],
    ["initialize", {}],
  ]);
  assert.equal(first[0].result.accepted, true);
  assert.notEqual(first[0].result.files[0].sourceId, first[0].result.files[1].sourceId);
  assert.equal(first[0].result.files[0].sha256, digest(sourceText));
  assert.ok(first[1].result.files.every((file) => !file.changed && file.revision === 1));
  assert.equal(first[2].result.messages[0].direction, "Rx");
  assert.equal(first[2].result.messages[0].canId, 291);
  assert.equal(first[3].error.code, -32602);
  assert.equal(first[4].result.accepted, false);
  assert.equal(first[4].result.issues[0].code, "DBC_CHANNEL_FRAME_CONFLICT");
  assert.equal(first[5].result.files.length, 2);
  assert.equal(first[6].error.code, -32602);
  assert.equal(first[7].result.accepted, false);
  assert.deepEqual(first[8].result, first[5].result);
  assert.equal(first[9].result.readOnly, true);
  assert.equal(first[9].result.buildExecuted, false);
  assert.equal(first[9].result.eclipseProject, true);
  for (const file of first[9].result.files) {
    assert.equal(file.sha256, digest(await readFile(path.join(first[9].result.root, file.path))));
  }
  assert.equal(first[10].error.code, -32602);
  assert.ok(first[11].result.supportedMethods.includes("dbc.import"));
  const projectClone = path.join(directory, "project-clone");
  await cp(path.join(repository, "fixtures/synthetic/eclipse_can_project"), projectClone, { recursive: true });
  await symlink(path.join(directory, "one"), path.join(projectClone, "external-link"), "junction");
  const links = requestSequence([["project.inspect", { root: projectClone }], ["project.inspect", { root: path.join(projectClone, "external-link") }]]);
  assert.ok(links[0].result.issues.some((issue) => issue.code === "PROJECT_LINK_NOT_FOLLOWED"));
  assert.ok(links[0].result.files.every((file) => !file.path.startsWith("external-link/")));
  assert.equal(links[1].error.code, -32602);
  const restarted = requestSequence([["dbc.sources", {}], ["network.ecu", { channel: "CAN2", ecu: "A" }]]);
  assert.deepEqual(restarted[0].result, first[5].result);
  assert.equal(restarted[1].result.messages[0].direction, "Tx");
  const updated = requestSequence([["dbc.import", { files: [{ ...a, text: a.text.replace("(0.1,0)", "(0.2,0)") }] }]]);
  assert.equal(updated[0].result.accepted, true);
  assert.equal(updated[0].result.files[0].revision, 2);
  assert.equal(updated[0].result.files[0].sourceId, first[0].result.files[0].sourceId);
  assert.equal(await readFile(a.sourceUri, "utf8"), sourceText);
  assert.equal((await stat(a.sourceUri)).mtimeMs, originalTime);
  console.log("DBC import, restart, channel conflicts, ECU queries and read-only inventory protocol tests passed.");
} finally {
  await rm(directory, { recursive: true, force: true });
}
