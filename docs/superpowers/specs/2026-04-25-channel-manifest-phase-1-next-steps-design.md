# Channel Manifest Phase 1 Next Steps Design

## Background And Goal

This document corrects and restructures the current follow-up framing around
`channel-manifest-phase-1`. Its purpose is not to redefine Phase 1 from
scratch, but to reconcile the current implementation state with
`channel-manifest-phase-1下一步工作安排.md` and provide clear recommendations for
what belongs in Phase 1 closeout versus later follow-up work.

The key outcome is a tighter boundary: Phase 1 should be treated as a mostly
completed implementation effort with a small amount of scope-freezing and
documentation alignment left, not as an open-ended bucket for new metadata or
binding-parity expansion.

## Current State Correction

The current branch state is ahead of parts of the "next steps" document.
Several items that still appear as follow-up implementation work have already
been completed and verified on `channel-manifest-phase-1`.

### Completed On The Implementation Branch

- Core Phase 1 channel manifest and attach validation behavior is implemented.
- Node and Python smoke coverage are both wired into the Phase 1 test gate.
- Python manifest mismatch handling is covered by a smoke test and exposes
  structured error fields for status and layout error classification.
- Node loading logic has been hardened to search multi-config addon output
  directories, reducing cross-platform packaging risk.

### Implication For Planning

The remaining work should no longer be framed as "finish core manifest
implementation." The remaining work is primarily:

- correcting stale planning language
- freezing the intended Phase 1 boundary
- deciding what moves into a later manifest-evolution or bindings-parity stage

## Unresolved Topics And Recommendations

### Creator Timestamp And Flags

Recommendation: defer creator timestamp and creator flags to a later phase and
do not include them in the Phase 1 or v1 manifest contract.

Reasoning:

- They do not change the correctness of the current attach-time validation
  contract.
- They enlarge the manifest surface area without helping close the current
  implementation stage.
- They would create avoidable follow-up work across C++, C API, Node, and
  Python bindings while the project is still trying to freeze the Phase 1
  boundary.

### Binding Parity As A Phase 1 Exit Criterion

Recommendation: do not treat full binding parity as a Phase 1 exit criterion.
Move parity work into a later, explicit bindings-parity stage.

Reasoning:

- Phase 1 is about landing and validating the core manifest contract, not about
  making every binding equally expressive.
- The current branch already includes targeted binding hardening that supports
  Phase 1 validation and observability.
- Expanding the Phase 1 gate to require broader builder or reader parity would
  change the project from a contract-completion effort into a much larger API
  harmonization effort.

### Future Manifest Incompatibility Versioning

Recommendation: introduce a distinct manifest-version concept in a later design
stage rather than reusing the existing layout-version concept.

Reasoning:

- Layout version describes binary/layout compatibility semantics.
- Manifest version should describe attach-time metadata contract semantics.
- Keeping them separate makes evolution clearer, reduces overloaded meaning in
  error handling, and gives bindings a cleaner compatibility story.

## Recommended Phase 1 Exit Boundary

Phase 1 should be considered complete when the following conditions are true:

- the core attach-time manifest validation behavior is stable
- C++ and supported bindings expose enough error information to diagnose
  manifest mismatch failures
- Node and Python smoke coverage are part of the automated Phase 1 verification
  path
- documentation reflects the actual implemented boundary

Phase 1 should explicitly exclude:

- creator timestamp and creator flags
- a new manifest-versioning scheme
- full builder or reader parity across all bindings
- broader metadata expansion that does not change the current validation
  correctness story

## Follow-Up Stage Breakdown

### Stage 0: Documentation Reconciliation And Scope Freeze

Goal:
Align planning docs with the actual branch state and freeze the Phase 1
boundary.

Includes:

- correcting stale "still pending" items
- restating the actual Phase 1 exit criteria
- marking deferred items as later-stage work rather than hidden Phase 1 debt

Excludes:

- new binding API expansion
- new manifest fields

### Stage 1: Phase 1 Closeout

Goal:
Finish only the remaining low-risk closeout work tied to the existing Phase 1
contract.

Includes:

- final wording and documentation cleanup
- confirmation that mismatch behavior expectations are stated consistently
- small verification or test-coverage alignment that does not expand scope

Excludes:

- new compatibility concepts
- new metadata semantics

### Stage 2: Manifest Evolution Design

Goal:
Define how manifest compatibility should evolve after Phase 1.

Includes:

- separate manifest-version semantics
- explicit compatibility and error-model rules
- future contract evolution guidance for bindings

Excludes:

- unrelated binding API growth

### Stage 3: Bindings Parity And Metadata Expansion

Goal:
Handle broader API parity and optional metadata growth as a separate delivery
track.

Includes:

- parity decisions for builder or reader capabilities across bindings
- evaluation of creator timestamp and flag support
- future convenience APIs that are useful but non-blocking for Phase 1

## Recommended Framing For Future Planning

The project should use the following framing going forward:

Phase 1 core implementation is effectively complete. The remaining work is to
freeze the Phase 1 boundary, correct planning artifacts, and avoid pulling
later-stage manifest evolution or binding-parity work back into the Phase 1
closeout.

This framing keeps Phase 1 shippable, gives later work a cleaner design entry
point, and prevents "one more field" or "one more binding method" from
indefinitely extending the current milestone.
