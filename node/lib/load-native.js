"use strict";

const fs = require("fs");
const load = require("node-gyp-build");
const path = require("path");

module.exports = function loadNative(rootDir) {
  try {
    return load(rootDir);
  } catch (packagedError) {
    const buildDir = path.join(rootDir, "..", "build");
    const multiConfigCandidates = ["Release", "Debug", "RelWithDebInfo", "MinSizeRel"].map((config) =>
      path.join(buildDir, "node", config, "xproc.node"),
    );
    const candidates = [
      path.join(rootDir, "xproc.node"),
      path.join(buildDir, "node", "xproc.node"),
      ...multiConfigCandidates,
      path.join(buildDir, "Release", "xproc.node"),
      path.join(buildDir, "Debug", "xproc.node"),
    ];

    let lastError = packagedError;
    for (const candidate of candidates) {
      if (!fs.existsSync(candidate)) {
        continue;
      }
      try {
        return require(candidate);
      } catch (error) {
        lastError = error;
      }
    }

    const message = [
      "Unable to load xproc native addon.",
      "Tried packaged prebuilds and local CMake build outputs.",
      `Candidates: ${candidates.join(", ")}`,
      lastError ? `Last error: ${lastError.message}` : null,
    ]
      .filter(Boolean)
      .join(" ");
    throw new Error(message);
  }
};
