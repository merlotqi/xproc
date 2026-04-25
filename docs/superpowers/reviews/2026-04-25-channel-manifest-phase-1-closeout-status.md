# Channel Manifest Phase 1 Closeout Status

## Purpose

This note records the corrected closeout status for `channel-manifest-phase-1`
after the Node and Python binding-hardening work landed. It replaces stale
"still pending" framing with a concise statement of what is now implemented,
what has been intentionally deferred, and how that differs from the earlier
next-steps note.

Phase 1 core implementation is effectively complete, but closeout still
requires documentation alignment and scope freeze.

## Implemented Since The Initial Assessment

- Node smoke coverage is now part of the Phase 1 gate through
  `xproc_run_phase1_tests`.
- Python smoke coverage now includes manifest mismatch assertions instead of
  covering only the success path.
- Python `XprocError` now exposes structured `status` and `layout_error`
  fields for manifest mismatch handling.
- Node addon loading now searches multi-config output directories, reducing
  packaging and local-build lookup risk.

## Items That Are No Longer Open Implementation Work

- Core manifest-backed attach validation is already implemented and verified.
- The supported Node and Python bindings are no longer an out-of-gate follow-up
  for Phase 1 smoke verification.
- The supported smoke-tested bindings no longer have an open Phase 1
  mismatch-observability gap.

## Deferred By Recommendation

- Creator timestamp and creator flags remain explicitly deferred rather than
  being pulled into the Phase 1 or v1 manifest contract.
- Full bindings parity remains deferred to a separate bindings-parity stage,
  not treated as a Phase 1 exit requirement.
- A separate manifest-version contract design remains deferred to a later
  manifest-evolution stage instead of being decided inside Phase 1 closeout.

## Delta Versus The Earlier Next-Steps Note

- The earlier note treated binding smoke automation and Python mismatch
  hardening as remaining implementation work; that is now outdated.
- The remaining Phase 1 closeout work is documentation alignment and boundary
  freezing, not additional core manifest delivery.
- The closeout boundary is now clearer: Phase 1 includes Node smoke coverage
  in the gate, Python mismatch assertions in smoke coverage, and structured
  Python `XprocError` metadata, while the three deferred topics stay outside
  the milestone. The Node addon loading hardening is a landed improvement from
  this closeout period, but not part of the formal Phase 1 exit boundary.

## Verification Snapshot

- This snapshot is limited to implementation-state and gate-registration
  verification gathered during documentation closeout; it does not mean all
  remaining Phase 1 closeout work is complete.
- Verification was executed in the
  `channel-manifest-phase-1` implementation worktree build directory at
  `/home/merlot/codes/xproc/.worktrees/channel-manifest-phase-1/build`,
  because that is where the implementation artifacts and test registration
  being confirmed live.
- `cmake --build /home/merlot/codes/xproc/.worktrees/channel-manifest-phase-1/build --target xproc_run_phase1_tests --parallel`
  passed on 2026-04-25 with `100% tests passed, 0 tests failed out of 18`.
- `ctest --test-dir /home/merlot/codes/xproc/.worktrees/channel-manifest-phase-1/build -N | rg "xproc_(node|python)_smoke"`
  still shows both `xproc_node_smoke` and `xproc_python_smoke` registered in
  the test set.
