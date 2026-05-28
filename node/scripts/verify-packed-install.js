"use strict";

const childProcess = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");

const packageDir = path.resolve(__dirname, "..");
const tempRoot = fs.mkdtempSync(path.join(os.tmpdir(), "xproc-pack-"));
const projectDir = path.join(tempRoot, "project");
fs.mkdirSync(projectDir, { recursive: true });
const useShell = process.platform === "win32";

const packOutput = childProcess.execFileSync("npm", ["pack", "--json"], {
  cwd: packageDir,
  encoding: "utf8",
  shell: useShell,
});
const [{ filename }] = JSON.parse(packOutput);
const tarballPath = path.join(packageDir, filename);

childProcess.execFileSync("npm", ["init", "-y"], { cwd: projectDir, stdio: "ignore", shell: useShell });
childProcess.execFileSync("npm", ["install", tarballPath], { cwd: projectDir, stdio: "inherit", shell: useShell });
childProcess.execFileSync(
  "node",
  [
    "-e",
    `
const xproc = require("@merlotqi/xproc");
const path = "/xproc_pack_verify_" + process.pid;
try { xproc.shmUnlink(path); } catch {}
const created = xproc.shm.createFixedChannel({ path, itemSize: 4, dataCapacity: 4096n });
const producer = created.openProducer();
const consumer = xproc.shm.attachFixedChannel({ path }).openConsumer();
producer.sendFixedSized(Buffer.from([1, 2, 3, 4]));
const payload = consumer.pollCopy();
if (!payload) throw new Error("expected payload after packaged install");
consumer.close();
producer.close();
try { xproc.shmUnlink(path); } catch {}
`,
  ],
  { cwd: projectDir, stdio: "inherit" },
);
