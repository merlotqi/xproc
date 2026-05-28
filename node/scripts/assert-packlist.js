"use strict";

const assert = require("node:assert/strict");
const childProcess = require("node:child_process");
const path = require("node:path");

const packageDir = path.resolve(__dirname, "..");
const requireAllPrebuilds = process.argv.includes("--require-all-prebuilds")
  || process.env.XPROC_REQUIRE_ALL_PREBUILDS === "1";
const requiredPrebuildFiles = [
  "prebuilds/linux-x64/node.napi.glibc.node",
  "prebuilds/linux-arm64/node.napi.glibc.node",
  "prebuilds/darwin-x64/node.napi.node",
  "prebuilds/darwin-arm64/node.napi.node",
  "prebuilds/win32-x64/node.napi.node",
];
const useShell = process.platform === "win32";
const raw = childProcess.execFileSync("npm", ["pack", "--dry-run", "--json"], {
  cwd: packageDir,
  encoding: "utf8",
  shell: useShell,
});

const [packResult] = JSON.parse(raw);
const files = new Set(packResult.files.map((entry) => entry.path));

assert.equal(packResult.name, "@merlotqi/xproc");
assert.ok([...files].some((file) => file.startsWith("prebuilds/")), "expected packaged prebuilds/");
if (requireAllPrebuilds) {
  for (const file of requiredPrebuildFiles) {
    assert.ok(files.has(file), `expected packaged ${file}`);
  }
}
assert.ok(files.has("index.js"));
assert.ok(files.has("index.d.ts"));
assert.ok(files.has("README.md"));
assert.ok(![...files].some((file) => file.startsWith("src/")), "did not expect native source files");
assert.ok(![...files].some((file) => file.startsWith("test/")), "did not expect node tests");
assert.ok(![...files].some((file) => file.startsWith("tsconfig")), "did not expect tsconfig files");
