# Channel Manifest Phase 1 Closeout Status

## Purpose

This note records the corrected closeout status for `channel-manifest-phase-1`
after the Node and Python binding-hardening work landed. It replaces stale
"still pending" framing with a concise statement of what is now implemented,
what has been intentionally deferred, and how that differs from the earlier
next-steps note.

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
- The supported Node and Python bindings now participate in the Phase 1 smoke
  verification path rather than remaining as out-of-gate follow-up.
- Cross-language mismatch observability for the current supported bindings is
  no longer an open hardening gap because Python now exposes structured
  `XprocError` metadata and mismatch smoke coverage.

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
  in the gate, Python mismatch assertions in smoke coverage, structured Python
  `XprocError` metadata, and hardened Node addon loading, while the three
  deferred topics stay outside the milestone.

## Verification Snapshot

- `cmake --build /home/merlot/codes/xproc/.worktrees/channel-manifest-phase-1/build --target xproc_run_phase1_tests --parallel`
  passed on 2026-04-25 with `100% tests passed, 0 tests failed out of 18`.
- `ctest --test-dir /home/merlot/codes/xproc/.worktrees/channel-manifest-phase-1/build -N | rg "xproc_(node|python)_smoke"`
  confirmed both `xproc_node_smoke` and `xproc_python_smoke` are registered in
  the test set.
