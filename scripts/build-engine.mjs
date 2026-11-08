// Native MinGW fallback for Windows environments without CMake. No shell or policy changes.
import { spawnSync } from "node:child_process";
import { existsSync, mkdirSync, statSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const outputArgument = process.argv.find((argument) => argument.startsWith("--output="));
const output = path.resolve(root, outputArgument?.slice("--output=".length) || "build/engine");
mkdirSync(output, { recursive: true });
function run(command, args) {
  const result = spawnSync(command, args, { cwd: root, stdio: "inherit", windowsHide: true });
  if (result.error) throw result.error;
  if (result.status !== 0) process.exit(result.status || 1);
}
const shared = ["app/json", "dbc/parser", "dbc/diff", "project/project",
  "project/project_store", "project/digest", "project/project_inventory"];
const flags = ["-std=c++20", "-Wall", "-Wextra", "-Wpedantic", "-pthread",
  "-Iengine/include", "-Ithird_party/sqlite"];
const objects = shared.map((file) => path.join(output, `${file.replaceAll("/", "_")}.o`));
const sqliteObject = path.join(output, "sqlite3.o");
if (!existsSync(sqliteObject) || ["third_party/sqlite/sqlite3.c", "third_party/sqlite/sqlite3.h"]
  .some((file) => statSync(path.join(root, file)).mtimeMs > statSync(sqliteObject).mtimeMs)) {
  run("gcc", ["-std=c99", "-O2", "-DSQLITE_THREADSAFE=1", "-c", "third_party/sqlite/sqlite3.c", "-o", sqliteObject]);
}
for (const [index, file] of shared.entries()) {
  run("g++", [...flags, "-c", `engine/src/${file}.cpp`, "-o", objects[index]]);
}
objects.push(path.join(output, "sqlite3.o"));
const extension = process.platform === "win32" ? ".exe" : "";
for (const [source, name] of [["src/app/main", "studio-engine"],
  ["tests/test_main", "studio-engine-tests"], ["tests/test_acceptance", "studio-acceptance-tests"]]) {
  const executable = path.join(output, name + extension);
  run("g++", [...flags, `engine/${source}.cpp`, ...objects, "-o", executable]);
  if (name !== "studio-engine") run(executable, []);
}
