const assert = require("node:assert/strict") as typeof import("node:assert/strict");
const test = require("node:test") as typeof import("node:test");

const xproc = require("../index.js") as XprocModule;

test("layout error enum includes fixedItemSizeIncompatible", () => {
  assert.equal(xproc.LAYOUT_ERROR.fixedItemSizeIncompatible, 11);
  assert.ok(xproc.layoutErrorString(11).length > 0);
});
