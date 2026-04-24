# Channel Manifest Phase 1 Audit

Date: 2026-04-24
Audit branch: `ai/superpowers`
Review target: `channel-manifest-phase-1`
Diff baseline: `main...channel-manifest-phase-1`
Design spec: `docs/superpowers/specs/2026-04-24-channel-manifest-phase-1-analysis-design.md`

## Scope

This audit reviews the `channel-manifest-phase-1` branch for:

- Confirmed bugs or regression risks
- Public contract mismatches across code, tests, and docs
- Testing gaps in the targeted Phase 1 gate
- Binding-layer alignment risks
- Residual decisions that may block or distort later work

## Evidence Reviewed

- `git diff --stat main..channel-manifest-phase-1`
- `git diff --name-only main..channel-manifest-phase-1`
- `git log --oneline --decorate main..channel-manifest-phase-1`
- `rg -n "Phase 1|manifest|builder|schema_id|attach_" README.md docs include tests capi node Python examples`
- `docs/todo.md`
- `README.md`
- `docs/building.rst`
- `docs/quickstart.rst`
- `examples/README.md`
- `examples/fixed_channel_inprocess.cpp`
- `examples/varlen_channel_inprocess.cpp`
- `include/xproc/xproc.hpp`
- `include/xproc/ipc/shm_builders.hpp`
- `include/xproc/ipc/options.hpp`
- `include/xproc/shm/shm_layout.hpp`
- `include/xproc/shm/shm_layout_manager.hpp`
- `src/shm/shm_layout_manager.cpp`
- `tests/CMakeLists.txt`
- `tests/api_surface_test.cpp`
- `tests/layout_validate_test.cpp`
- `tests/capi_smoke_test.cpp`
- `capi/xproc_c.cpp`
- `.github/workflows/ci.yml`
- `node/CMakeLists.txt`
- `node/package.json`
- `node/test/smoke.test.ts`
- `Python/CMakeLists.txt`
- `Python/xproc/__init__.pyi`

## Findings

### High Risk

- No confirmed high-risk defects were identified during this review.

### Medium Risk

- The Node binding's Phase 1 smoke and mismatch coverage is present in source but not executed by the repository's automated test flows. Evidence: `node/test/smoke.test.ts` is the only reviewed test that asserts JavaScript schema-mismatch metadata, `node/package.json` defines an `npm test` entry for it, `node/CMakeLists.txt` only builds `xproc.node`, and `.github/workflows/ci.yml` runs `xproc_run_phase1_tests` plus `ctest` without any Node test step. Impact: regressions in JavaScript option normalization or `layoutError` reporting can land without an automated signal even though the branch now ships a Node binding and examples. Recommendation: wire the Node smoke suite into CI, either through a CTest wrapper target or an explicit `npm test` step, and decide whether the schema-mismatch case belongs in the named Phase 1 regression gate.
- The Python binding does not yet provide a tested, typed manifest-mismatch contract comparable to the C and Node surfaces. Evidence: `Python/tests/smoke_test.py` covers only a successful fixed-channel roundtrip, `Python/src/python_binding.cpp` registers `status_error` as `XprocError` without attaching structured fields, `Python/xproc/__init__.pyi` declares `class XprocError(RuntimeError): ...`, and the reviewed mismatch-path assertions exist only in `tests/capi_smoke_test.cpp` and `node/test/smoke.test.ts`. Impact: Python callers have no verified programmatic way to distinguish schema/layout mismatches from generic open failures, and regressions in that failure path are unlikely to be caught before release. Recommendation: add a Python mismatch smoke test and either expose structured error metadata on `XprocError` or explicitly document `last_layout_error()` as the supported Python compatibility-check contract.

### Observations and Recommendations

- The C++ Phase 1 core appears internally consistent: `include/xproc/shm/shm_layout.hpp`, `src/shm/shm_layout_manager.cpp`, and `include/xproc/ipc/shm_builders.hpp` line up on manifest fields, attach-time validation, inferred shared-memory sizing, and creator versus readonly attach behavior.
- The public C++ workflow is consistently documented and exemplified: `README.md`, `docs/quickstart.rst`, `examples/README.md`, `examples/fixed_channel_inprocess.cpp`, and `examples/varlen_channel_inprocess.cpp` all use `make_*_channel(...).create(...)` for creators and `attach_*_channel(...)` for non-creators.
- The remaining unchecked items in `docs/todo.md` read more like explicit Phase 1 scope and contract decisions, such as builder parity expectations and future manifest-version semantics, than like missing C++ implementation work.

## Verification Notes

- Static evidence review completed across the Phase 1 diff, core SHM files, public docs/examples, binding code, test registration, and CI wiring.
- Per task instructions, no build or runtime verification target was executed in this task, including `xproc_run_phase1_tests`.

## Overall Conclusion

The reviewed C++ Phase 1 implementation appears safe for its stated core goals: the control block now carries the needed manifest metadata, attach helpers infer existing shared-memory sizing, mismatch errors are typed, and the documented builder workflow matches the shipped examples. I did not confirm any high-risk defects in the shared-memory layout manager, builder API, or C++-level attach contract. The main residual risk is binding-layer parity around failure handling, where JavaScript mismatch coverage is not automated and Python mismatch behavior is not yet tested or typed as a stable contract. Those unfinished items look like binding verification and product-contract gaps, not evidence that the underlying Phase 1 C++ manifest work is broken.
