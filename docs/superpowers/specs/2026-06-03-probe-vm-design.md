# Probe VM: Lightweight Monitoring Bytecode Design

Date: 2026-06-03
Branch target: `ai-superpowers`
Implementation target: `feat/probe-vm`
Phase 1 MVP scope

## Objective

Add a lightweight bytecode virtual machine to xproc for runtime monitoring and diagnostics. Users inject small programs (similar to eBPF) that execute at hook points in the send/consume pipeline, reading message metadata and ring state, writing results to shared counter maps.

This enables dynamic, user-defined monitoring logic without modifying the library or restarting processes. Programs are verified for safety (no loops, bounded execution), implemented in pure C for portability, and support license injection for commercial use.

## Problem Statement

Current monitoring in xproc is limited to:

- `ring_snapshot` — raw atomic fields, read by observer
- `diagnostics_tracker` — hardcoded liveness/idle tracking
- `observer::occupancy_ratio()` etc. — hardcoded derived metrics

If a user wants to count high-priority messages, detect fill-ratio thresholds, or track per-schema-id throughput, they must write their own polling loop, read snapshots, and compute results externally. There is no way to inject monitoring logic into the send/consume hot path itself.

eBPF solved this for the Linux kernel: small verified programs attached to hook points, running in a sandboxed VM. xproc needs an equivalent for shared-memory IPC monitoring.

## Recommended Direction

A pure-C bytecode VM with:

- **Custom minimal ISA** — 10 instructions, 32-bit fixed-width encoding, 8 registers
- **Shared maps** — counter arrays in SHM, cross-process visible
- **Hybrid execution** — synchronous inline hooks (on_send, on_consume) + future async observer hooks
- **License injection** — programs carry a license, verified before execution
- **Static verification** — no backward jumps, max 256 instructions, must end with HALT

## Architecture

```
                    ┌─────────────────────────┐
                    │     xproc probe VM       │
                    │     (pure C)             │
  ┌──────────┐     │  ┌───────────┐           │     ┌──────────┐
  │ Producer │────►│  │  on_send  │──►prog[]──►│     │  Shared  │
  │ send()   │     │  │  hook     │   execute  │────►│  Maps    │
  └──────────┘     │  └───────────┘           │     │(counters)│
                    │                          │     └──────────┘
  ┌──────────┐     │  ┌───────────┐           │          │
  │ Consumer │────►│  │on_consume │──►prog[]──►│          ▼
  │ poll()   │     │  │  hook     │   execute  │     ┌──────────┐
  └──────────┘     │  └───────────┘           │     │ User reads│
                    └─────────────────────────┘     │ maps via  │
                                                     │ API       │
                                                     └──────────┘
```

## ISA Design

32-bit fixed-width encoding. 8 general-purpose registers (R0-R7).

### Instruction encoding

```
[opcode: 8 bits] [dst: 3 bits] [src: 3 bits] [imm/offset: 18 bits]
```

### Instruction set

| Mnemonic | Opcode | Encoding | Semantics |
|----------|--------|----------|-----------|
| `LOAD_IMM` | 0x01 | `dst, imm18` | `R[dst] = sign_extend(imm18)` |
| `LOAD_CTX` | 0x02 | `dst, field` | `R[dst] = context[field]` |
| `LOAD_MAP` | 0x03 | `dst, map_idx` | `R[dst] = map[map_idx]` |
| `STORE_MAP` | 0x04 | `src, map_idx` | `map[map_idx] = R[src]` |
| `ADD` | 0x05 | `dst, src` | `R[dst] = R[dst] + R[src]` |
| `SUB` | 0x06 | `dst, src` | `R[dst] = R[dst] - R[src]` |
| `CMP_GT` | 0x07 | `dst, src` | `flags = (R[dst] > R[src]) ? 1 : 0` (unsigned) |
| `JMP_IF_NOT` | 0x08 | `offset18` | `if (flags == 0) pc += offset18` |
| `INC_MAP` | 0x09 | `map_idx` | `map[map_idx] += 1` (atomic) |
| `HALT` | 0x00 | — | Stop execution |

**Note:** Phase 1 has no `MUL`/`DIV`. For percentage thresholds, pre-compute the threshold value at program creation time (e.g. `threshold = capacity * 80 / 100`) and load it as an immediate. `MUL`/`DIV` may be added in Phase 2 if needed.

### Context fields (for LOAD_CTX)

| Field code | Name | Type |
|-----------|------|------|
| 0 | `meta.user_data` | u64 |
| 1 | `meta.timestamp_ns` | u64 |
| 2 | `meta.schema_id` | u32 |
| 3 | `meta.flags` | u32 |
| 4 | `ring.write_pos` | u64 |
| 5 | `ring.read_pos` | u64 |
| 6 | `ring.capacity` | u64 |
| 7 | `payload_len` | u32 |

### Instruction macros (C)

```c
#define XPROBE_OPCODE_LOAD_IMM   0x01
#define XPROBE_OPCODE_LOAD_CTX   0x02
#define XPROBE_OPCODE_LOAD_MAP   0x03
#define XPROBE_OPCODE_STORE_MAP  0x04
#define XPROBE_OPCODE_ADD        0x05
#define XPROBE_OPCODE_SUB        0x06
#define XPROBE_OPCODE_CMP_GT     0x07
#define XPROBE_OPCODE_JMP_IF_NOT 0x08
#define XPROBE_OPCODE_INC_MAP    0x09
#define XPROBE_OPCODE_HALT       0x00

#define XPROBE_LOAD_IMM(dst, imm) \
  (((XPROBE_OPCODE_LOAD_IMM) << 24) | ((dst) << 21) | ((imm) & 0x3FFFF))
#define XPROBE_LOAD_CTX(dst, field) \
  (((XPROBE_OPCODE_LOAD_CTX) << 24) | ((dst) << 21) | ((field) & 0x7))
#define XPROBE_LOAD_MAP(dst, idx) \
  (((XPROBE_OPCODE_LOAD_MAP) << 24) | ((dst) << 21) | ((idx) & 0x3FFFF))
#define XPROBE_STORE_MAP(src, idx) \
  (((XPROBE_OPCODE_STORE_MAP) << 24) | ((src) << 21) | ((idx) & 0x3FFFF))
#define XPROBE_ADD(dst, src) \
  (((XPROBE_OPCODE_ADD) << 24) | ((dst) << 21) | ((src) << 18))
#define XPROBE_SUB(dst, src) \
  (((XPROBE_OPCODE_SUB) << 24) | ((dst) << 21) | ((src) << 18))
#define XPROBE_CMP_GT(dst, src) \
  (((XPROBE_OPCODE_CMP_GT) << 24) | ((dst) << 21) | ((src) << 18))
#define XPROBE_JMP_IF_NOT(offset) \
  (((XPROBE_OPCODE_JMP_IF_NOT) << 24) | ((offset) & 0x3FFFF))
#define XPROBE_INC_MAP(idx) \
  (((XPROBE_OPCODE_INC_MAP) << 24) | ((idx) & 0x3FFFF))
#define XPROBE_HALT() \
  (XPROBE_OPCODE_HALT << 24)
```

## Shared Maps

Phase 1 supports one map type: **counter array**.

```c
typedef struct xprobe_map xprobe_map;

xprobe_map*  xprobe_map_create(const char* name, uint32_t size);
void         xprobe_map_destroy(xprobe_map* map);
uint64_t     xprobe_map_read(const xprobe_map* map, uint32_t index);
void         xprobe_map_reset(xprobe_map* map, uint32_t index);
void         xprobe_map_reset_all(xprobe_map* map);
```

Map layout in SHM:

```
[name: 64 bytes] [size: u32] [counters: size * u64]
```

Maps are created in SHM and can be accessed from any process that opens the same SHM path.

## C API

### Program lifecycle

```c
typedef struct xprobe_license {
  const char* data;
  uint32_t    len;
} xprobe_license;

xprobe_prog* xprobe_prog_create(const uint32_t* instructions, uint32_t count,
                                 const xprobe_license* license);
void         xprobe_prog_destroy(xprobe_prog* prog);
```

### Execution context

```c
typedef struct xprobe_context {
  uint64_t user_data;
  uint64_t timestamp_ns;
  uint32_t schema_id;
  uint32_t flags;
  uint64_t write_pos;
  uint64_t read_pos;
  uint64_t capacity;
  uint32_t payload_len;
} xprobe_context;

typedef struct xprobe_result {
  int      status;    // 0 = ok, <0 = error
  uint32_t events;    // EMIT event bitmask
  uint32_t steps;     // instructions executed
} xprobe_result;

xprobe_result xprobe_exec(const xprobe_prog* prog, xprobe_map* map,
                           const xprobe_context* ctx);
```

### Hook attachment

```c
typedef enum xprobe_hook {
  XPROBE_HOOK_ON_SEND     = 0,
  XPROBE_HOOK_ON_CONSUME  = 1,
} xprobe_hook;

typedef struct xprobe_handle xprobe_handle;

xprobe_handle* xprobe_attach(xprobe_prog* prog, xprobe_map* map, xprobe_hook hook);
void           xprobe_detach(xprobe_handle* handle);
```

### License verification

```c
typedef int (*xprobe_license_verify_fn)(const xprobe_license* license, void* user_data);
void xprobe_set_license_verify(xprobe_license_verify_fn fn, void* user_data);
```

Default behavior (no verify function set): all programs pass license check.

### Error codes

```c
#define XPROBE_OK                  0
#define XPROBE_ERR_INVALID_PROG   -1
#define XPROBE_ERR_LICENSE        -2
#define XPROBE_ERR_MAP_FULL       -3
#define XPROBE_ERR_MAX_STEPS      -4
```

## File Structure

| Action | File | Responsibility |
|--------|------|----------------|
| Create | `include/xproc/probe/probe.h` | Public C API |
| Create | `include/xproc/probe/probe_instructions.h` | Instruction encoding macros |
| Create | `include/xproc/probe/probe_map.h` | Map types |
| Create | `include/xproc/probe/probe_vm.h` | VM execution engine |
| Create | `src/probe/probe_vm.c` | VM implementation |
| Create | `src/probe/probe_map.c` | Map implementation |
| Create | `src/probe/probe_verify.c` | Bytecode verifier |
| Create | `tests/probe_test.c` | C unit tests |
| Modify | `tests/CMakeLists.txt` | Register new test |
| Modify | `include/xproc/ipc/channel.hpp` | Add hook dispatch in send/poll |
| Modify | `tests/capi_smoke_test.cpp` | Add C API probe smoke tests |
| Delete | `include/xproc/ipc/inspector.hpp` | `ring_snapshot`, `ring_inspector_interface` |
| Delete | `include/xproc/ipc/diagnostics_tracker.hpp` | `diagnostics_tracker` class |
| Delete | `src/ipc/diagnostics_tracker.cpp` | `diagnostics_tracker` implementation |
| Modify | `include/xproc/ipc/observer.hpp` | Remove diagnostic methods, keep `peek()` and `header()` |
| Modify | `tests/ipc_diagnostics_test.cpp` | Delete or rewrite for probe VM |
| Modify | `capi/xproc_c.h` | Remove snapshot/diagnostics C API |
| Modify | `capi/xproc_c.cpp` | Remove snapshot/diagnostics C API |

## Superseded Components (to be removed)

The probe VM replaces the following hardcoded diagnostic components. They should be removed in the same branch or immediately after:

| Removed | Probe VM equivalent |
|---------|-------------------|
| `ring_snapshot` struct | `xprobe_context` — same fields (write_pos, read_pos, commit_seq, etc.) plus message_meta |
| `ring_inspector_interface` | No replacement needed — observer uses probe hooks |
| `attach_count_view_interface` | Accessible via `LOAD_CTX` if needed in future |
| `diagnostics_tracker` class | A probe program that tracks `commit_seq` delta and `timestamp_ns` idle |
| `observer::occupancy_ratio()` | Probe program: `LOAD_CTX(write_pos)`, `LOAD_CTX(read_pos)`, `SUB`, `LOAD_CTX(capacity)`, `CMP_GT` |
| `observer::occupancy_bytes()` | Probe program: `LOAD_CTX(write_pos)`, `LOAD_CTX(read_pos)`, `SUB` |
| `observer::available_bytes()` | Probe program: `LOAD_CTX(capacity)`, used_bytes, `SUB` |
| `observer::consumer_lag_bytes()` | Same as occupancy_bytes (was always identical) |
| `observer::snapshot()` | No replacement — observer exposes `header()` for direct atomic reads |
| C API: `xproc_c_observer_snapshot` | `xprobe_exec` with probe program |
| C API: `xproc_c_observer_occupancy_*` | `xprobe_exec` + map read |
| C API: `xproc_c_diagnostics_tracker_*` | `xprobe_exec` with liveness probe program |

The `observer` class itself is retained — it still provides read-only SHM access and `peek()`. But its diagnostic methods and `ring_snapshot` dependency are removed. The observer becomes a pure data-access layer; monitoring logic lives in probe programs.

The C++ `diagnostics_tracker` header and source files are deleted. Users who need liveness tracking write a probe program instead.

## Non-Goals

- JIT compilation (Phase 3)
- Histogram map type (Phase 2)
- Event ring buffer output (Phase 2)
- Async observer-side execution (Phase 2)
- Indirect jumps or function calls
- Accessing payload data from VM (only metadata and ring state)
- Multi-threaded VM execution (single-threaded per hook point)

## Verification Rules

The verifier (`probe_verify.c`) runs at `xprobe_prog_create` time:

1. Instruction count ≤ 256
2. No backward jumps (`JMP_IF_NOT` offset must be > 0)
3. Jump targets within program bounds (`pc + offset < count`)
4. Register indices in range [0, 7]
5. Map indices in range [0, map_size)
6. Program must end with `HALT`

Failure returns NULL from `xprobe_prog_create`.

## API Compatibility

| Component | Changes | Breaking? |
|-----------|---------|-----------|
| `include/xproc/probe/` | New headers | No -- new |
| `src/probe/` | New source files | No -- new |
| `channel::send_*` | Internal hook dispatch | No -- invisible to caller |
| `channel::poll` | Internal hook dispatch | No -- invisible to caller |
| `ring_snapshot` | **Removed** | **Yes** |
| `ring_inspector_interface` | **Removed** | **Yes** |
| `attach_count_view_interface` | **Removed** | **Yes** |
| `diagnostics_tracker` | **Removed** | **Yes** |
| `observer::occupancy_ratio()` | **Removed** | **Yes** |
| `observer::occupancy_bytes()` | **Removed** | **Yes** |
| `observer::available_bytes()` | **Removed** | **Yes** |
| `observer::consumer_lag_bytes()` | **Removed** | **Yes** |
| `observer::snapshot()` | **Removed** | **Yes** |
| `observer::used_bytes_()` | **Removed** | **Yes** |
| `xproc_c_observer_snapshot` | **Removed** | **Yes** |
| `xproc_c_observer_occupancy_*` | **Removed** | **Yes** |
| `xproc_c_observer_available_bytes` | **Removed** | **Yes** |
| `xproc_c_observer_consumer_lag_bytes` | **Removed** | **Yes** |
| `xproc_c_diagnostics_tracker_*` | **Removed** | **Yes** |
| `control_block` | Unchanged | No |

## Testing Strategy

### C unit tests (`tests/probe_test.c`)

- `ProbeCreateDestroy` — program and map lifecycle
- `ProbeVerifyNoBackJump` — backward jump rejected
- `ProbeVerifyMaxInstructions` — >256 instructions rejected
- `ProbeVerifyMustEndHalt` — non-HALT ending rejected
- `ProbeExecLoadImm` — `LOAD_IMM` sets register correctly
- `ProbeExecLoadCtx` — `LOAD_CTX` reads context fields correctly
- `ProbeExecIncMap` — `INC_MAP` increments counter
- `ProbeExecCounterThreshold` — combined counter + threshold program
- `ProbeExecLicenseRejected` — license verify fails → `XPROBE_ERR_LICENSE`
- `ProbeExecLicensePassed` — license verify passes → execution proceeds
- `ProbeMapReadWrite` — map read, write, reset

### C API smoke tests (`tests/capi_smoke_test.cpp`)

- `CProbeCreateSmoke` — create/destroy program via C API
- `CProbeMapSmoke` — map read/write via C API
- `CProbeExecSmoke` — execute program, read counter result

### Integration tests

- `ProbeExecOnSendHook` — `channel::send_varlen` triggers attached probe

### Removal validation

- All existing tests compile and pass after removing `ring_snapshot`, `diagnostics_tracker`, observer diagnostics
- Observer still works for `peek()` and `header()` access
- C API smoke tests pass after removing snapshot/diagnostics functions
- No references to removed types in `include/xproc/xproc.hpp`

## Success Criteria

- Pure C VM core compiles without C++ dependencies
- Verifier rejects all unsafe programs
- License verification failure prevents execution
- No probe attached → zero overhead (single null check)
- Probe attached → correct execution, map updated
- Cross-process map sharing works via SHM

## Evidence Sources

- `include/xproc/ipc/channel.hpp` — send/poll methods where hooks attach
- `include/xproc/ipc/message_meta.hpp` — per-message metadata structure
- `include/xproc/core/layout_types.hpp` — control_block with ring state
- `include/xproc/ipc/observer.hpp` — existing read-only observer pattern
- `include/xproc/ipc/diagnostics_tracker.hpp` — existing hardcoded diagnostics

## Transition Rule

After this spec is reviewed and approved, the next step is to write the implementation plan using the `writing-plans` skill, targeting `feat/probe-vm`.
