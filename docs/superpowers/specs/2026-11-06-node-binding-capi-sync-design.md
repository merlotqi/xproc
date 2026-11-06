# Node Binding C API Sync Design Spec

**Date**: 2026-11-06
**Status**: Approved
**Scope**: `node/` binding (native addon + JS/TS layer) — catch up to the current C API; no `capi/` changes

## Goal

The Node binding was last updated 2026-05-28 and has drifted behind the C API. Bring it to full parity with the **existing** C API surface:

| Gap | C API commit |
|-----|--------------|
| `xproc_c_message_meta` send (`send_fixed_sized_with_meta`, `send_varlen_with_meta`) and observer read (`peek_copy_with_meta`) | `7cfbf33` (2026-06-04) |
| Diagnostics: 4 observer getters + `xproc_c_diagnostics_tracker_*` | `a71d476` (2026-06-02) |
| `XPROC_C_LAYOUT_ERROR_FIXED_ITEM_SIZE_INCOMPATIBLE` (11) | `a285657` (2026-09-25) |

Explicit non-goals:

- **No `capi/` changes.** Consumer-side meta reads stay out (only `observer.peek_copy_with_meta` exists in C; the unused `xproc_c_poll_handler` typedef is not wrapped).
- **No subprocess exposure** (C++-only API today, no C layer yet).
- Baseline is green (build + 9 tests + typecheck pass); this is additive, not a fix.

API shape: **Approach 1 — idiomatic JS full parity**: sends take an optional trailing meta argument (no duplicated `send*WithMeta` methods); the one new `peekCopyWithMeta` name exists because peek's return type changes (`{ payload, meta }`), which an optional argument cannot express cleanly.

## Types

```ts
// Input for sends; every field optional, missing fields default to 0.
interface MessageMetaInput {
  userData?: IntegerLike;    // u64
  timestampNs?: IntegerLike; // u64
  schemaId?: IntegerLike;    // u32
  flags?: IntegerLike;       // u32
}

// Output from peek; mapped by integer width.
interface MessageMeta {
  userData: bigint;    // u64 → bigint
  timestampNs: bigint; // u64 → bigint
  schemaId: number;    // u32 → number
  flags: number;       // u32 → number
}
```

- Input normalization happens in `index.js` via the existing `normalizeIntegerLike` (same pattern as transport options). The native layer strictly validates types and throws `TypeError` otherwise.
- `LayoutError` union gains `| 11`; the constant is exported at all three layers: native `export_constants` → `index.js` `LAYOUT_ERROR` map → `index.d.ts`.
  - `schemaId` in `MessageMeta` output is `number` (width-based mapping), which differs from `ResolvedTransportOptions.schemaId: bigint`; this is intentional and documented in JSDoc.

## Producer

- Native `Producer.sendFixedSized(data, meta?)` and `Producer.sendVarlen(data, meta?)`:
  - no `meta` argument → existing C call (`xproc_c_producer_send_fixed_sized` / `xproc_c_producer_send_varlen`);
  - `meta` present → `xproc_c_producer_send_*_with_meta` with a fully populated `xproc_c_message_meta`.
- `sendFixedBytes` unchanged (no C counterpart).
- `index.js` `Producer` wrapper passes the normalized meta through: `super.sendVarlen(coerceBuffer(data), meta)`.
- JSDoc + README note: **fixed-channel meta is accepted for API compatibility only and is not transported**; fixed-channel readers receive zero-initialized meta (matches C semantics, covered by test).

## Observer

- `peekCopyWithMeta(): { payload: Uint8Array; meta: MessageMeta } | null` (runtime payload is a `Buffer`, declared as `Uint8Array` to match the existing `peekCopy` typing)
  - Reuses the two-step `BUFFER_TOO_SMALL` retry pattern from `peekCopy`.
  - Empty channel (`XPROC_C_STATUS_AGAIN`) → `null`.
- Synchronous diagnostics getters (thin wrappers over the C calls):
  - `occupancyRatio(): number` (double, [0, 1])
  - `occupancyBytes(): bigint`, `availableBytes(): bigint`, `consumerLagBytes(): bigint` (u64 → bigint)
- `peekCopy` / `peekCopyInto` unchanged; `Consumer` untouched (no C-level consumer meta).

## DiagnosticsTracker

New ObjectWrap obtained via `observer.createDiagnosticsTracker()`:

```ts
class DiagnosticsTracker {
  update(): void;
  producerAlive(): boolean;
  idleMs(): bigint;   // u64 → bigint
  close(): void;      // explicit destroy; destructor also destroys (RAII)
}
```

**Lifetime safety** — the C contract states the observer must outlive the tracker (destroying the observer first is UB):

- The tracker holds a `Napi::ObjectReference` to its observer so GC cannot collect the observer while the tracker is alive.
- `observer_wrap` gains an `active_trackers_` counter. `Observer.close()` throws `TypeError` while the counter is `> 0` — the same guard pattern `Consumer.close()` already uses for pending `waitAsync`.
- Tracker `close()` (and its destructor) decrements the counter. Calling tracker methods after `close()` throws `TypeError`.
- `createDiagnosticsTracker()` on a closed observer fails via the native `validate_handle` path (standard xproc error).

## Tests

New file `node/test/meta_diagnostics.test.ts`:

1. **Varlen meta roundtrip** — send with all four fields set (explicit `timestampNs`) → `peekCopyWithMeta` returns them unchanged.
2. **Fixed-channel compat** — send with meta → peeked meta is all zeros; send without meta → all zeros as well.
3. **Diagnostics getters smoke** — ratio ∈ [0, 1], bytes ≤ data capacity, lag ≥ 0 on a partially filled channel.
4. **Tracker** — after send + `update()`, `producerAlive() === true`; `idleMs() >= 0`; guard: `observer.close()` throws while the tracker is open; after `tracker.close()` the observer closes normally.
5. **Enum** — `LAYOUT_ERROR.fixedItemSizeIncompatible === 11` and `layoutErrorString(11)` returns a non-empty string.

Verification:

```bash
cmake --build build --target xproc_node
cd node && npm run typecheck && npm test
ctest --test-dir build -R xproc_node_smoke
```

## Documentation

- `index.d.ts`: JSDoc on every new API (existing style), including the fixed-channel meta caveat and the `MessageMeta.schemaId` width note.
- `node/README.md`: meta send/peek quick-start snippet + diagnostics snippet.
- Root `CHANGELOG.md`: one entry under `Unreleased → Added`.

## Files Touched

| File | Action |
|------|--------|
| `node/src/node_binding.cpp` | Add optional meta args, `peekCopyWithMeta`, 4 getters, `DiagnosticsTracker` wrap, layout-error constant export, `active_trackers_` guard |
| `node/index.js` | Thread meta through `Producer` wrapper; add `fixedItemSizeIncompatible` to `LAYOUT_ERROR` |
| `node/index.d.ts` | `MessageMetaInput` / `MessageMeta` / `DiagnosticsTracker` types, method signatures, `LayoutError` `| 11`, JSDoc |
| `node/test/meta_diagnostics.test.ts` | **New** — 5 test groups above |
| `node/README.md` | Meta + diagnostics examples |
| `CHANGELOG.md` | Unreleased → Added entry |
