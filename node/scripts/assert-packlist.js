"use strict";

const assert = require("node:assert/strict");
const childProcess = require("node:child_process");
const path = require("node:path");

const packageDir = path.resolve(__dirname, "..");
const raw = childProcess.execFileSync("npm", ["pack", "--dry-run", "--json"], {
  cwd: packageDir,
  encoding: "utf8",
});

const [packResult] = JSON.parse(raw);
const files = new Set(packResult.files.map((entry) => entry.path));

assert.equal(packResult.name, "@merlot/xproc");
assert.ok([...files].some((file) => file.startsWith("prebuilds/")), "expected packaged prebuilds/");
assert.ok(files.has("index.js"));
assert.ok(files.has("index.d.ts"));
assert.ok(files.has("README.md"));
assert.ok(![...files].some((file) => file.startsWith("src/")), "did not expect native source files");
assert.ok(![...files].some((file) => file.startsWith("test/")), "did not expect node tests");
assert.ok(![...files].some((file) => file.startsWith("tsconfig")), "did not expect tsconfig files");
