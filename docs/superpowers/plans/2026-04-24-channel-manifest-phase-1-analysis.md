# Channel Manifest Phase 1 Analysis Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce an evidence-backed audit and Phase 1 checklist assessment for `channel-manifest-phase-1` on the `ai/superpowers` branch, then commit the analysis artifacts.

**Architecture:** Keep the approved design spec as the governing document, then create two linked Markdown outputs: one audit organized by finding severity and one Phase 1 assessment organized by checklist item. Build both documents from the same evidence set so code-review conclusions, checklist statuses, and verification notes stay aligned.

**Tech Stack:** Git, Markdown, ripgrep, sed, CMake/CTest, xproc C++/C/Node/Python source tree.

---

## File Structure

- Existing spec: `docs/superpowers/specs/2026-04-24-channel-manifest-phase-1-analysis-design.md`
- Create: `docs/superpowers/audits/2026-04-24-channel-manifest-phase-1-audit.md`
- Create: `docs/superpowers/reviews/2026-04-24-channel-manifest-phase-1-phase1-assessment.md`
- Reference: `docs/todo.md`
- Reference: `README.md`
- Reference: `docs/building.rst`
- Reference: `docs/quickstart.rst`
- Reference: `examples/README.md`
- Reference: `include/xproc/xproc.hpp`
- Reference: `include/xproc/ipc/shm_builders.hpp`
- Reference: `include/xproc/ipc/options.hpp`
- Reference: `include/xproc/shm/shm_layout.hpp`
- Reference: `include/xproc/shm/shm_layout_manager.hpp`
- Reference: `src/shm/shm_layout_manager.cpp`
- Reference: `tests/CMakeLists.txt`
- Reference: `tests/api_surface_test.cpp`
- Reference: `tests/layout_validate_test.cpp`
- Reference: `tests/capi_smoke_test.cpp`
- Reference: `examples/fixed_channel_inprocess.cpp`
- Reference: `examples/varlen_channel_inprocess.cpp`
- Reference: `capi/xproc_c.h`
- Reference: `capi/xproc_c.cpp`
- Reference: `node/index.js`
- Reference: `node/index.d.ts`
- Reference: `node/test/smoke.test.ts`
- Reference: `Python/src/python_binding.cpp`
- Reference: `Python/tests/smoke_test.py`
- Reference: `.github/workflows/ci.yml`

### Task 1: Scaffold the Analysis Documents and Lock the Evidence Baseline

**Files:**
- Create: `docs/superpowers/audits/2026-04-24-channel-manifest-phase-1-audit.md`
- Create: `docs/superpowers/reviews/2026-04-24-channel-manifest-phase-1-phase1-assessment.md`
- Reference: `docs/superpowers/specs/2026-04-24-channel-manifest-phase-1-analysis-design.md`
- Reference: `docs/todo.md`
- Reference: `README.md`
- Reference: `tests/CMakeLists.txt`

- [ ] **Step 1: Create the audit document with the approved structure**

```md
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

- Approved design spec
- `main...channel-manifest-phase-1` diff and commit range
- `docs/todo.md`
- Public headers, docs, examples, and targeted tests

## Findings

### High Risk

- No confirmed high-risk defects were identified during the initial scaffold pass.

### Medium Risk

- No medium-risk findings recorded yet.

### Observations and Recommendations

- Evidence collection is still in progress.

## Verification Notes

- Verification has not been recorded yet.

## Overall Conclusion

The audit is not complete until the findings and verification sections are updated from repository evidence.
```

- [ ] **Step 2: Create the Phase 1 assessment document with checklist rows seeded from `docs/todo.md`**

```md
# Channel Manifest Phase 1 Assessment

Date: 2026-04-24
Assessment branch: `ai/superpowers`
Review target: `channel-manifest-phase-1`
Diff baseline: `main...channel-manifest-phase-1`
Design spec: `docs/superpowers/specs/2026-04-24-channel-manifest-phase-1-analysis-design.md`
Checklist source: `docs/todo.md`

## Assessment Rules

- Status values are limited to `Completed`, `Partially completed`, `Not completed`, and `Requires decision`.
- Each row must cite repository evidence from code, tests, docs, or commits.
- Open product or architecture choices are tracked as `Requires decision`, not as implementation defects.

## Phase 1 Checklist

| Area | Checklist Item | Status | Evidence | Notes |
| --- | --- | --- | --- | --- |
| Manifest / attach validation | Control block stores layout type, capacity, alignment, fixed item size, and schema metadata | Pending review | `include/xproc/shm/shm_layout.hpp` | Verify manifest fields against the checklist wording |
| Manifest / attach validation | Producer, consumer, and observer attach paths fail with typed layout errors | Pending review | `include/xproc/ipc/shm_builders.hpp`, `src/shm/shm_layout_manager.cpp`, `tests/layout_validate_test.cpp` | Confirm attach-time failure modes and error codes |
| Manifest / attach validation | Default attach flows infer existing SHM size in the common case | Pending review | `include/xproc/ipc/shm_builders.hpp`, `tests/api_surface_test.cpp`, `tests/layout_validate_test.cpp` | Verify non-creators avoid hand-wiring channel shape |
| Manifest / attach validation | Creator timestamp and flags are either part of the v1 contract or explicitly deferred | Pending review | `docs/todo.md`, `include/xproc/shm/shm_layout.hpp` | Determine whether the branch resolves or defers this decision |
| Manifest / attach validation | Public docs describe manifest-backed validation rather than manual low-level wiring | Pending review | `README.md`, `docs/quickstart.rst`, `docs/building.rst` | Check whether user-facing guidance matches the new contract |
| Builder ergonomics | C++ fixed and varlen builders exist for create and attach flows | Pending review | `include/xproc/ipc/shm_builders.hpp`, `include/xproc/xproc.hpp` | Confirm the public builder API surface |
| Builder ergonomics | Builder layer covers producer, consumer, and observer attach scenarios | Pending review | `include/xproc/ipc/shm_builders.hpp`, `tests/api_surface_test.cpp` | Confirm all three endpoint types are supported |
| Builder ergonomics | At least one fixed and one varlen example use the builder API | Pending review | `examples/fixed_channel_inprocess.cpp`, `examples/varlen_channel_inprocess.cpp`, `examples/README.md` | Check docs and examples together |
| Builder ergonomics | Binding parity is either included in Phase 1 or explicitly deferred | Pending review | `docs/todo.md`, `capi/xproc_c.h`, `node/index.js`, `Python/src/python_binding.cpp` | Record the actual branch position on parity |
| Validation / mismatch coverage | Regression coverage exists for version mismatch and layout type mismatch | Pending review | `tests/layout_validate_test.cpp` | Confirm exact tests and names |
| Validation / mismatch coverage | Binding-level mismatch coverage exists for manifest failures | Pending review | `tests/capi_smoke_test.cpp`, `node/test/smoke.test.ts`, `Python/tests/smoke_test.py` | Record which bindings actually exercise mismatch behavior |
| Validation / mismatch coverage | Fixed-channel item-size mismatch has dedicated C++ coverage | Pending review | `tests/layout_validate_test.cpp` | Confirm test name and coverage scope |
| Validation / mismatch coverage | Alignment mismatch has dedicated C++ coverage | Pending review | `tests/layout_validate_test.cpp` | Confirm test name and attach path |
| Validation / mismatch coverage | Observer mismatch coverage exists for wrong layout type and schema or version mismatch | Pending review | `tests/layout_validate_test.cpp`, `tests/capi_smoke_test.cpp` | Confirm observer-specific tests and limits |
| Validation / mismatch coverage | Manifest-version incompatibility contract is defined and tested | Pending review | `docs/todo.md`, `include/xproc/shm/shm_layout_manager.hpp`, `tests/layout_validate_test.cpp` | Determine whether current version fields are sufficient or still undecided |
| Done gate | README, examples, and docs reflect the final Phase 1 workflow | Pending review | `README.md`, `docs/building.rst`, `docs/quickstart.rst`, `examples/README.md` | Confirm final workflow wording is consistent |
| Done gate | A targeted Phase 1 regression suite is easy to run locally and in CI | Pending review | `tests/CMakeLists.txt`, `README.md`, `.github/workflows/ci.yml` | Confirm target name, regex coverage, and docs |
| Done gate | Phase 2 work does not rely on low-level attach details that Phase 1 was supposed to hide | Pending review | `docs/todo.md`, public APIs, examples, bindings | Assess whether low-level options still leak into common usage |

## Summary

No checklist item should remain `Pending review` once the assessment is complete.
```

- [ ] **Step 3: Run the baseline diff and evidence discovery commands**

Run: `git diff --stat main..channel-manifest-phase-1`  
Expected: A file-level diff summary showing the branch touches core C++, tests, docs, examples, and bindings.

Run: `git diff --name-only main..channel-manifest-phase-1`  
Expected: A changed-file list that can be copied into the evidence sections of both documents.

Run: `git log --oneline --decorate main..channel-manifest-phase-1`  
Expected: A linear list of the 15 commits currently ahead of `main`.

Run: `rg -n "Phase 1|manifest|builder|schema_id|attach_" README.md docs include tests capi node Python examples`  
Expected: Hits that identify the exact files discussing the new attach contract and its regression coverage.

- [ ] **Step 4: Replace the initial evidence sections with the concrete command set and source-file list**

```md
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
- `.github/workflows/ci.yml`
```

- [ ] **Step 5: Commit the document scaffolding**

Run: `git add docs/superpowers/audits/2026-04-24-channel-manifest-phase-1-audit.md docs/superpowers/reviews/2026-04-24-channel-manifest-phase-1-phase1-assessment.md`  
Expected: No output.

Run: `git commit -m "docs: scaffold channel manifest phase 1 analysis"`  
Expected: A commit is created on `ai/superpowers` with the two new Markdown files.

### Task 2: Populate the Audit with Verified Findings

**Files:**
- Modify: `docs/superpowers/audits/2026-04-24-channel-manifest-phase-1-audit.md`
- Reference: `README.md`
- Reference: `docs/building.rst`
- Reference: `docs/quickstart.rst`
- Reference: `examples/README.md`
- Reference: `include/xproc/xproc.hpp`
- Reference: `include/xproc/ipc/shm_builders.hpp`
- Reference: `include/xproc/ipc/options.hpp`
- Reference: `include/xproc/shm/shm_layout.hpp`
- Reference: `include/xproc/shm/shm_layout_manager.hpp`
- Reference: `src/shm/shm_layout_manager.cpp`
- Reference: `tests/CMakeLists.txt`
- Reference: `tests/api_surface_test.cpp`
- Reference: `tests/layout_validate_test.cpp`
- Reference: `tests/capi_smoke_test.cpp`
- Reference: `capi/xproc_c.h`
- Reference: `capi/xproc_c.cpp`
- Reference: `node/index.js`
- Reference: `node/index.d.ts`
- Reference: `node/test/smoke.test.ts`
- Reference: `Python/src/python_binding.cpp`
- Reference: `Python/tests/smoke_test.py`

- [ ] **Step 1: Inspect the core SHM builder and manifest implementation files**

Run: `sed -n '1,220p' include/xproc/xproc.hpp`  
Expected: The umbrella header re-exporting the builder API for public consumption.

Run: `sed -n '1,260p' include/xproc/ipc/shm_builders.hpp`  
Expected: Public create and attach helper definitions for fixed and varlen channels.

Run: `sed -n '1,220p' include/xproc/ipc/options.hpp`  
Expected: Public transport option fields such as `schema_id` and existing-size inference semantics.

Run: `sed -n '1,220p' include/xproc/shm/shm_layout.hpp`  
Expected: Control-block manifest fields such as layout type, alignment, fixed item size, and schema metadata.

Run: `sed -n '1,260p' include/xproc/shm/shm_layout_manager.hpp`  
Expected: Layout validation interfaces and version helpers used during attach.

Run: `sed -n '1,260p' src/shm/shm_layout_manager.cpp`  
Expected: Validation implementation showing how mismatches become typed errors.

- [ ] **Step 2: Inspect public contract and example files that claim the new workflow**

Run: `sed -n '1,260p' README.md`  
Expected: Quick-start guidance, builder examples, and a documented Phase 1 regression target.

Run: `sed -n '1,220p' docs/building.rst`  
Expected: A build-and-test guide that mentions the focused regression suite.

Run: `sed -n '1,220p' docs/quickstart.rst`  
Expected: User-facing attach and channel-creation guidance that should match the builder contract.

Run: `sed -n '1,220p' examples/README.md`  
Expected: Example descriptions that stay consistent with the current fixed and varlen workflows.

Run: `sed -n '1,220p' examples/fixed_channel_inprocess.cpp`  
Expected: A fixed-channel example using the builder API if the docs claim that migration already happened.

Run: `sed -n '1,220p' examples/varlen_channel_inprocess.cpp`  
Expected: A varlen example using the builder API if the docs claim that migration already happened.

- [ ] **Step 3: Inspect binding and test coverage for parity and mismatch behavior**

Run: `sed -n '1,260p' tests/CMakeLists.txt`  
Expected: The `xproc_run_phase1_tests` target and its regex definition.

Run: `sed -n '1,360p' tests/api_surface_test.cpp`  
Expected: Builder round-trip coverage and public-API regression tests.

Run: `sed -n '1,360p' tests/layout_validate_test.cpp`  
Expected: Layout mismatch, alignment, version, and observer attach failure coverage.

Run: `sed -n '1,320p' tests/capi_smoke_test.cpp`  
Expected: Binding-facing mismatch coverage for the C API.

Run: `sed -n '1,260p' capi/xproc_c.h`  
Expected: The exposed C surface for shared-memory options and any builder-parity gaps.

Run: `sed -n '1,260p' node/index.d.ts`  
Expected: Public Node declarations that reveal whether the new builder contract is surfaced.

Run: `sed -n '1,320p' node/index.js`  
Expected: Actual Node runtime helpers and whether they mirror the C++ attach ergonomics.

Run: `sed -n '1,260p' Python/src/python_binding.cpp`  
Expected: Python-exposed shared-memory flow and any manifest-related behavior.

Run: `sed -n '1,220p' Python/tests/smoke_test.py`  
Expected: Python smoke coverage and whether mismatch paths are exercised there.

- [ ] **Step 4: Replace the scaffold findings with verified conclusions only**

```md
## Findings

### High Risk

- If no confirmed issue reaches this severity, keep exactly one bullet: `No confirmed high-risk defects were identified during this review.`

### Medium Risk

- Record only verified issues. Each bullet must be four sentences in this order: conclusion, `Evidence:` sentence, `Impact:` sentence, `Recommendation:` sentence.

### Observations and Recommendations

- Record non-blocking cleanup ideas, deferred decisions, or useful follow-up checks that do not qualify as confirmed defects.

## Overall Conclusion

Summarize in 3-5 sentences whether the branch appears safe for its stated Phase 1 goals, what residual risks remain, and whether the unfinished items are implementation gaps or explicit decisions.
```

- [ ] **Step 5: Commit the populated audit**

Run: `git add docs/superpowers/audits/2026-04-24-channel-manifest-phase-1-audit.md`  
Expected: No output.

Run: `git commit -m "docs: audit channel manifest phase 1 branch"`  
Expected: A commit is created containing only the audit document updates.

### Task 3: Populate the Phase 1 Checklist Assessment

**Files:**
- Modify: `docs/superpowers/reviews/2026-04-24-channel-manifest-phase-1-phase1-assessment.md`
- Reference: `docs/todo.md`
- Reference: `README.md`
- Reference: `docs/building.rst`
- Reference: `docs/quickstart.rst`
- Reference: `include/xproc/ipc/shm_builders.hpp`
- Reference: `include/xproc/shm/shm_layout.hpp`
- Reference: `include/xproc/shm/shm_layout_manager.hpp`
- Reference: `tests/CMakeLists.txt`
- Reference: `tests/api_surface_test.cpp`
- Reference: `tests/layout_validate_test.cpp`
- Reference: `tests/capi_smoke_test.cpp`
- Reference: `capi/xproc_c.h`
- Reference: `node/index.js`
- Reference: `node/test/smoke.test.ts`
- Reference: `Python/src/python_binding.cpp`
- Reference: `Python/tests/smoke_test.py`
- Reference: `examples/fixed_channel_inprocess.cpp`
- Reference: `examples/varlen_channel_inprocess.cpp`
- Reference: `examples/README.md`
- Reference: `.github/workflows/ci.yml`

- [ ] **Step 1: Review the Phase 1 checklist language directly from `docs/todo.md`**

Run: `sed -n '1,220p' docs/todo.md`  
Expected: The Phase 1 exit checklist and the unresolved decision items that must be classified as completed, partial, not completed, or requires decision.

Run: `sed -n '1,220p' examples/README.md`  
Expected: Example-level wording that either confirms or undermines the claimed final Phase 1 workflow.

Run: `sed -n '1,220p' .github/workflows/ci.yml`  
Expected: CI configuration showing whether the targeted Phase 1 gate is exercised in automation or whether the docs overstate current CI coverage.

- [ ] **Step 2: Update every checklist row from `Pending review` to a final status**

```md
## Assessment Rules

- Status values are limited to `Completed`, `Partially completed`, `Not completed`, and `Requires decision`.
- Every row must include evidence and a short note.
- `Requires decision` is reserved for open contract or scope choices that the branch leaves unresolved.
- `Partially completed` is reserved for items where implementation exists but docs, tests, or public contract coverage are incomplete.
```

- [ ] **Step 3: Add a summary section that answers the Phase 1 question directly**

```md
## Summary

- State whether the branch completes the implemented Phase 1 core.
- State which items remain open because they still need code or coverage.
- State which items remain open because the repository still needs a product or architecture decision.
- End with a single sentence saying whether Phase 1 is functionally complete, documentation-complete, or still materially open.
```

- [ ] **Step 4: Commit the populated assessment**

Run: `git add docs/superpowers/reviews/2026-04-24-channel-manifest-phase-1-phase1-assessment.md`  
Expected: No output.

Run: `git commit -m "docs: assess channel manifest phase 1 checklist"`  
Expected: A commit is created containing only the Phase 1 assessment document updates.

### Task 4: Verify Consistency, Run the Targeted Test Gate, and Finalize the Branch

**Files:**
- Modify: `docs/superpowers/audits/2026-04-24-channel-manifest-phase-1-audit.md`
- Modify: `docs/superpowers/reviews/2026-04-24-channel-manifest-phase-1-phase1-assessment.md`
- Reference: `tests/CMakeLists.txt`
- Reference: `README.md`

- [ ] **Step 1: Run the targeted Phase 1 regression gate from the existing build tree**

Run: `cmake --build build --target xproc_run_phase1_tests`  
Expected: The build invokes the focused test target and prints `Running the targeted xproc Phase 1 regression suite`; record whether it exits cleanly or which first failure appears.

- [ ] **Step 2: Record the observed verification result in both documents**

```md
## Verification Notes

- Command: `cmake --build build --target xproc_run_phase1_tests`
- Result: Record whether the target passed, failed, or was blocked.
- Relevance: Explain how the result supports or limits confidence in the audit conclusions.
```

- [ ] **Step 3: Cross-check the two documents for contradictions**

Run: `rg -n "High Risk|Medium Risk|Requires decision|Partially completed|Not completed|Verification Notes|Overall Conclusion" docs/superpowers/audits/2026-04-24-channel-manifest-phase-1-audit.md docs/superpowers/reviews/2026-04-24-channel-manifest-phase-1-phase1-assessment.md`  
Expected: A compact view of the final decision sections so you can verify that the assessment status rows and audit conclusions do not contradict each other.

- [ ] **Step 4: Run a clean diff check before the final commit**

Run: `git diff --check`  
Expected: No output, which confirms there are no whitespace or conflict-marker issues in the Markdown files.

Run: `git status --short`  
Expected: Only the two analysis documents appear as modified if this task is the only remaining work.

- [ ] **Step 5: Commit the final verification pass**

Run: `git add docs/superpowers/audits/2026-04-24-channel-manifest-phase-1-audit.md docs/superpowers/reviews/2026-04-24-channel-manifest-phase-1-phase1-assessment.md`  
Expected: No output.

Run: `git commit -m "docs: finalize channel manifest phase 1 analysis"`  
Expected: A final commit records any consistency or verification-note adjustments after the test run.
