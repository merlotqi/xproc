# Creator Metadata Manifest Design

## Background And Goal

`channel-manifest-phase-1` already persists and validates core shared-memory
manifest fields such as layout type, fixed item size, data alignment, data
capacity, and `schema_id`. The shared-memory control block still retains
`reserved[3]`, and the project has deferred a decision on whether creator
timestamp and creator flags should become first-class manifest fields.

This design defines the next step for those fields:

- add creator timestamp and creator flags as persisted manifest metadata
- expose them across C++, C API, Node, and Python
- keep them out of attach-time validation semantics for now

The goal is to make creator metadata observable and portable without silently
expanding the attach-compatibility contract.

## Recommended Decision

This design recommends introducing two new persisted metadata fields:

- `creator_timestamp_ns: uint64_t`
- `creator_flags: uint64_t`

These fields should be treated as **persisted creator metadata**, not as
attach-validation requirements.

That means:

- creators may set them when creating a channel
- attachers may read them after manifest-backed attach succeeds
- attach does not fail when local caller values differ from persisted values

This keeps the metadata useful for diagnostics, provenance, and light-weight
out-of-band coordination while avoiding premature compatibility coupling.

## Data Model

### Control Block Storage

The shared-memory control block should replace part of the current
`reserved[3]` area with named fields:

- `creator_timestamp_ns`
- `creator_flags`
- one remaining reserved 64-bit slot for future manifest growth

The timestamp uses nanoseconds so the persisted representation is explicit and
stable across bindings. The flags field remains an opaque 64-bit bitmask in
this phase; no individual flag meanings are standardized yet.

### Public Configuration Surface

`transport_options` should gain:

- `creator_timestamp_ns`
- `creator_flags`

These fields become the public configuration surface used by C++, the C API,
Node, and Python. The intent is to keep all binding layers working through the
same semantic option names instead of directly depending on header-layout
details.

## Read/Write Semantics

### Creator Path

When a creator configures and creates a shared-memory channel:

1. the caller may set `creator_timestamp_ns` and `creator_flags`
2. the create path writes those values into the manifest-backed control block
3. the creator continues using the configured values locally; no extra
   handshake is required

### Attacher Path

When an attacher opens an existing shared-memory channel:

1. normal manifest validation still runs using the existing validation fields
2. once attach succeeds, the implementation reads `creator_timestamp_ns` and
   `creator_flags` from the persisted control block
3. the attacher-side observable metadata reflects the persisted values

Attachers do **not** use these fields as local matching inputs in this phase.
If an attacher provides local values, those values are not compared against the
persisted manifest and must not trigger attach failure.

## Layering And Responsibilities

### `shm_layout_header`

The control block owns the durable storage format for the two new fields.

### `layout_manager`

The layout-manager layer is responsible for:

- writing the fields into the control block during creation
- reading the fields back from persisted storage
- keeping this logic centralized so bindings do not need to understand header
  internals

This layer should not introduce new mismatch validation for these fields.

### `transport_options`

`transport_options` is the semantic bridge across all public surfaces. It
should carry the two new fields with default value `0`, making the behavior
backward-compatible for callers that do not opt in.

### Builders And Attach Helpers

The builder layer should add explicit fluent setters:

- `with_creator_timestamp_ns(uint64_t)`
- `with_creator_flags(uint64_t)`

Creators use these setters to populate the manifest. Attachers may observe the
persisted values after attach, but the attach helpers do not treat them as
matching requirements.

### C API / Node / Python

All binding layers should expose the same two semantic fields:

- C API: fields on `xproc_transport_options_t`
- Node: `creatorTimestampNs`, `creatorFlags`
- Python: `creator_timestamp_ns`, `creator_flags`

The cross-language contract should be documented consistently:

`creator timestamp/flags are persisted metadata, not attach-validation requirements`

## API Surface Strategy

This design intentionally does **not** introduce a separate
`manifest_metadata` object.

Reasons:

- the current project already uses `transport_options` as the shared
  configuration surface
- the two new fields are lightweight and fit naturally beside `schema_id`
- introducing a new metadata object now would create extra API churn before
  there is enough metadata volume to justify it

If future manifest evolution adds more fields with richer semantics, the
project can revisit whether a dedicated metadata view object becomes worthwhile.

## Testing Strategy

### C++ Core

Add round-trip and readback coverage for:

- creator writes non-zero timestamp and flags
- attacher reads back the same persisted values
- default path reads back `0`

### Builder Coverage

Add coverage showing:

- builder fluent setters populate the create path
- attach succeeds without the attacher specifying either field
- persisted values remain observable after attach

### Binding Coverage

Each supported binding layer should gain at least one focused readback test:

- create path sets creator metadata
- attach/readback path observes the same persisted values
- no mismatch or validation error is introduced by the new fields

### Documentation Checks

User-facing docs should explicitly say the fields are persisted metadata only.
They should not be described as schema, version, or compatibility guards.

## Compatibility Boundaries

This phase keeps the compatibility story intentionally conservative.

### Defaults

Both new fields default to `0`.

This ensures:

- existing callers continue to work without modification
- callers that do not care about creator metadata see stable, neutral behavior

### Validation Behavior

No new attach failure mode is introduced for:

- `creator_timestamp_ns`
- `creator_flags`

Those fields remain outside the attach-validation contract in this phase.

### Future Evolution

If the project later wants certain creator flags to affect compatibility, that
should be treated as a separate manifest-contract design change with its own
spec, tests, and compatibility discussion.

## Success Criteria

This design is satisfied when:

- the control block stores named creator metadata fields instead of relying
  solely on anonymous reserved space
- C++, C API, Node, and Python can all write and read the new fields
- create-to-attach readback is covered by tests
- default `0` behavior is covered by tests
- no new attach-validation mismatch behavior is added for the fields

## Recommended Framing

This work should be framed as **manifest metadata expansion**, not as a Phase 1
validation extension.

It adds portable creator provenance fields to the manifest-backed storage model
while deliberately preserving the current attach-compatibility contract.
