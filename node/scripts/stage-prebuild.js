"use strict";

const fs = require("fs");
const path = require("path");

function readArg(flag) {
  const index = process.argv.indexOf(flag);
  return index >= 0 ? process.argv[index + 1] : undefined;
}

const source = path.resolve(readArg("--source") ?? path.join(__dirname, "..", "..", "build", "node", "xproc.node"));
const platform = readArg("--platform") ?? process.platform;
const arch = readArg("--arch") ?? process.arch;
const libc = readArg("--libc") ?? (platform === "linux" ? "glibc" : "");

const prebuildDir = path.join(__dirname, "..", "prebuilds", `${platform}-${arch}`);
const filename = platform === "linux" && libc === "glibc" ? "node.napi.glibc.node" : "node.napi.node";
const destination = path.join(prebuildDir, filename);

fs.mkdirSync(prebuildDir, { recursive: true });
fs.copyFileSync(source, destination);
console.log(destination);
