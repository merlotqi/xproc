# Channel Manifest Follow-Up Execution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Freeze the real Phase 1 boundary, reconcile stale planning artifacts with the current branch state, and separate later manifest-evolution work from current closeout work.

**Architecture:** Treat this as a documentation-and-boundary closeout, not a new feature push. The implementation branch should only update the Phase 1 checklist and user-facing status wording to match what is already built, while the `ai/superpowers` branch records the corrected state and the staged follow-up roadmap.

**Tech Stack:** Markdown docs, git worktrees, existing `xproc_run_phase1_tests` verification flow, CMake/CTest results already available on `channel-manifest-phase-1`.

---

## Scope

This plan intentionally covers only the follow-up work that is still actionable after the recent Node/Python binding hardening landed:

- reconcile stale planning language with current implementation reality
- freeze the Phase 1 exit boundary
- document which items are explicitly deferred to later stages
- preserve a clean handoff for later manifest-evolution and bindings-parity design

This plan does **not** implement:

- creator timestamp or creator flags
- a new manifest-version field
- full C / Node / Python builder parity
- new transport or runtime features outside the manifest closeout topic

## File Structure

- Create on `ai/superpowers`: `docs/superpowers/reviews/2026-04-25-channel-manifest-phase-1-closeout-status.md`
- Create on `ai/superpowers`: `docs/superpowers/plans/2026-04-25-channel-manifest-follow-up-execution-plan.md`
- Modify on `channel-manifest-phase-1`: `docs/todo.md`
- Modify on `channel-manifest-phase-1`: `README.md`
- Reference on `ai/superpowers`: `docs/superpowers/specs/2026-04-25-channel-manifest-phase-1-next-steps-design.md`
- Reference on `channel-manifest-phase-1`: `tests/CMakeLists.txt`
- Reference on `channel-manifest-phase-1`: `node/CMakeLists.txt`
- Reference on `channel-manifest-phase-1`: `Python/tests/smoke_test.py`

### Task 1: Publish a Corrected Phase 1 Status Note on `ai/superpowers`

**Files:**
- Create: `docs/superpowers/reviews/2026-04-25-channel-manifest-phase-1-closeout-status.md`
- Reference: `docs/superpowers/specs/2026-04-25-channel-manifest-phase-1-next-steps-design.md`
- Reference: `docs/superpowers/audits/2026-04-24-channel-manifest-phase-1-audit.md`
- Reference: `docs/superpowers/reviews/2026-04-24-channel-manifest-phase-1-phase1-assessment.md`

- [ ] **Step 1: Create the closeout-status review skeleton**

Add this markdown skeleton:

```md
# Channel Manifest Phase 1 Closeout Status

## Purpose

This note corrects stale follow-up language after the Phase 1 binding-hardening work landed.

## Implemented Since The Initial Assessment

- Node smoke coverage now participates in the Phase 1 regression gate.
- Python smoke coverage now includes manifest mismatch assertions.
- Python `XprocError` exposes structured `status` and `layout_error` fields.
- Node addon loading now searches multi-config output directories.

## Items That Are No Longer Open Implementation Work

- Core manifest-backed attach validation
- Higher-level C++ builder flows
- Cross-language mismatch observability for the current supported bindings

## Deferred By Recommendation

- Creator timestamp / flags
- Full bindings parity
- Separate manifest-version contract design
```

- [ ] **Step 2: Add a short “what changed vs the old next-steps note” section**

Append this section so later readers can reconcile the old working note without guessing:

```md
## Delta Versus The Earlier Next-Steps Note

- Items previously listed as pending binding automation are now complete.
- Remaining work is primarily boundary-freezing and documentation alignment.
- Future manifest evolution remains a later design topic, not hidden Phase 1 debt.
```

- [ ] **Step 3: Commit the `ai/superpowers` status note**

Run:

```bash
git -C .worktrees/ai-superpowers add docs/superpowers/reviews/2026-04-25-channel-manifest-phase-1-closeout-status.md
git -C .worktrees/ai-superpowers commit -m "docs: record phase 1 closeout status"
```

Expected:
- `git` creates one docs-only commit on `ai/superpowers`
- the review note is readable without opening the longer spec first

### Task 2: Freeze the Phase 1 Checklist on `channel-manifest-phase-1`

**Files:**
- Modify: `docs/todo.md`
- Reference: `README.md`
- Reference: `tests/CMakeLists.txt`
- Reference: `node/CMakeLists.txt`
- Reference: `Python/tests/smoke_test.py`

- [ ] **Step 1: Replace the three open decision checkboxes with explicit decisions**

Update the unchecked items in `docs/todo.md` so they read like this:

```md
- [x] Decide whether creator timestamp / flags are part of the supported v1 manifest contract or explicitly defer
      them to a later phase. Decision: defer to a later phase; do not include them in the Phase 1 / v1 contract.
```

```md
- [x] Decide whether builder parity for C / Node / Python is a Phase 1 exit criterion or is intentionally deferred to
      the bindings-parity phase. Decision: defer parity work to a dedicated bindings-parity stage.
```

```md
- [x] Decide whether a future manifest-version incompatibility should be represented by the existing layout version
      fields or by a separate manifest version field, then test that exact contract. Decision: use a separate
      manifest-version concept in a later design stage.
```

- [ ] **Step 2: Reword the remaining open Phase 2 dependency gate**

Adjust the last unchecked item so it stops sounding like blocked Phase 1 implementation work and instead captures the
handoff boundary:

```md
- [ ] Phase 2 follow-up work is scoped behind dedicated design and implementation plans rather than being pulled back
      into the Phase 1 closeout milestone.
```

Expected:
- `docs/todo.md` now distinguishes finished work, decided deferrals, and true remaining follow-up
- the checklist no longer implies Node/Python automation is still missing

- [ ] **Step 3: Commit the checklist freeze on the implementation branch**

Run:

```bash
git -C .worktrees/channel-manifest-phase-1 add docs/todo.md
git -C .worktrees/channel-manifest-phase-1 commit -m "docs: freeze phase 1 manifest checklist"
```

Expected:
- `channel-manifest-phase-1` gains a docs-only commit
- the Phase 1 checklist can be read as a boundary document instead of a stale backlog

### Task 3: Tighten the User-Facing Phase 1 Summary

**Files:**
- Modify: `README.md`
- Reference: `docs/todo.md`

- [ ] **Step 1: Add a short Phase 1 summary paragraph near the shared-memory workflow docs**

Insert this paragraph in `README.md` immediately after the existing sentence
`For the focused shared-memory builder / manifest / mismatch gate used by Phase 1 work:` and its code block:

```md
The current Phase 1 shared-memory workflow is manifest-backed: attachers validate channel shape from the stored
control-block metadata, and targeted regression coverage includes the supported Node and Python smoke checks for
manifest mismatch behavior. Additional metadata expansion and cross-binding parity remain later-stage follow-up work.
```

- [ ] **Step 2: Make sure the README does not promise deferred work as if it already exists**

Remove or rewrite any nearby wording that implies:

- creator timestamp / flags are already part of the public contract
- all bindings have feature parity with the C++ builders
- future manifest-version semantics are already defined

Expected:
- user-facing docs describe the actual shipped Phase 1 contract
- the README stays aligned with the deferred decisions captured in `docs/todo.md`

- [ ] **Step 3: Commit the README alignment**

Run:

```bash
git -C .worktrees/channel-manifest-phase-1 add README.md
git -C .worktrees/channel-manifest-phase-1 commit -m "docs: align phase 1 shared memory summary"
```

Expected:
- README wording matches the current implementation boundary
- no deferred feature is advertised as delivered

### Task 4: Re-Verify the Existing Phase 1 Gate and Record the Result

**Files:**
- Reference: `tests/CMakeLists.txt`
- Reference: `node/CMakeLists.txt`
- Reference: `Python/tests/smoke_test.py`
- Reference: `docs/superpowers/reviews/2026-04-25-channel-manifest-phase-1-closeout-status.md`

- [ ] **Step 1: Run the targeted Phase 1 verification command on the implementation branch**

Run:

```bash
cmake --build .worktrees/channel-manifest-phase-1/build --target xproc_run_phase1_tests --parallel
```

Expected:
- build completes successfully
- output includes the existing Phase 1 gate target rather than a narrower ad hoc test command

- [ ] **Step 2: Confirm the binding smoke tests are still part of the gate**

Run:

```bash
ctest --test-dir .worktrees/channel-manifest-phase-1/build -N | rg "xproc_(node|python)_smoke"
```

Expected:

```text
xproc_node_smoke
xproc_python_smoke
```

- [ ] **Step 3: Record the verification result in the `ai/superpowers` closeout-status note**

Append a short verification block like:

```md
## Verification Snapshot

- `cmake --build build --target xproc_run_phase1_tests --parallel` passes on `channel-manifest-phase-1`
- `ctest -N` confirms both `xproc_node_smoke` and `xproc_python_smoke` are in the registered test set
```

- [ ] **Step 4: Commit the verification note update on `ai/superpowers`**

Run:

```bash
git -C .worktrees/ai-superpowers add docs/superpowers/reviews/2026-04-25-channel-manifest-phase-1-closeout-status.md
git -C .worktrees/ai-superpowers commit -m "docs: record phase 1 verification snapshot"
```

Expected:
- the review note contains execution evidence, not just narrative conclusions

## Success Criteria

- `ai/superpowers` contains a concise closeout-status review and this execution plan
- `channel-manifest-phase-1/docs/todo.md` encodes the agreed deferments as explicit decisions
- `channel-manifest-phase-1/README.md` describes the shipped Phase 1 boundary without promising deferred work
- the current `xproc_run_phase1_tests` gate is rerun and its Node/Python smoke coverage is recorded
- later manifest evolution and bindings parity remain explicitly staged as follow-up work rather than hidden Phase 1 debt
