# Socket Test Coverage Gap-Fill Design

Date: 2026-05-30
Branch target: `ai-superpowers`
Phase 2 reference: [2026-05-28-phase2-reference-design.md](../reference/2026-05-28-phase2-reference-design.md)
Implementation target: `feat/socket-test-coverage-gap-fill`

## Objective

Close the 3 remaining test coverage gaps identified in the P2 Socket Test Coverage audit, and update the Phase 2 reference design checklist to reflect actual state.

The current `socket_transport_test.cpp` has 19 test cases covering varlen/fixed loopback, disconnect/reconnect, partial-frame recovery, runtime integration, wait/interrupt, and validation. This spec fills the last holes.

## Background

A 2026-05-30 audit of the current test suite against the Phase 2 reference design found that 2 of 4 P2 Socket Test Coverage checklist items were already complete (disconnect/reconnect tests, runtime over socket_consumer), 2 were partial (fixed-frame roundtrip, dual-stack edge cases). Three concrete gaps remain:

| # | Gap | Reason |
|---|-----|--------|
| 1 | No `FixedTcpLoopbackIPv4` | Varlen has both IPv4 and IPv6 loopback; fixed only has IPv6 |
| 2 | No `send_fixed_bytes` test | Only the exact-size path (`send_fixed_sized`) is exercised; zero-padded path is untested |
| 3 | No dual-stack binding test | `IPV6_V6ONLY=0` + IPv4 fallback in `start_listen()` has no explicit coverage |

## Gap 1: FixedTcpLoopbackIPv4

**Test:** `FixedTcpLoopbackIPv4`

Calls the existing `run_fixed_loopback("127.0.0.1")` helper, symmetric with `FixedTcpLoopbackIPv6` which calls `run_fixed_loopback("::1")`.

One-line test body. No new helper needed.

## Gap 2: `send_fixed_bytes` (zero-padded)

**Test:** `FixedBytesZeroPaddedRoundtrip`

The socket wire format for fixed channels always sends exactly `item_size` bytes. `send_fixed_bytes` pads short data with zeros; `send_fixed_sized` requires exact length and asserts otherwise.

Test:
- Create a fixed channel with `item_size = 8`
- Producer calls `send_fixed_bytes` with 4 bytes of data
- Consumer receives 8 bytes: first 4 match the sent data, last 4 are zero

This exercises the `byte_length < item_size` branch that `send_fixed_sized` tests never hit.

## Gap 3: Dual-Stack Binding

**Test: `SingleListenerServesBothIPv4AndIPv6`**

`tcp_listen` (via `try_resolved_stream_socket`) attempts IPv4 bind first, then IPv6 with dual-stack (`IPV6_V6ONLY=0`) as a fallback. On most modern platforms the IPv6 dual-stack path wins because `getaddrinfo(AI_PASSIVE, AF_UNSPEC)` returns IPv6 entries first. In either path, the resulting listener should accept both address families. The existing tests (`VarlenTcpLoopbackIPv4`, `FixedTcpLoopbackIPv6`, `VarlenTcpLoopbackIpv6BracketHost`) each create separate consumer/producer pairs, so they don't verify that a single listener serves both families.

Test:
- Create one consumer with default bind (no `socket_host`)
- Producer A connects via `127.0.0.1` (IPv4), sends a varlen message, consumer receives it, producer A destructs
- Producer B connects via `::1` (IPv6), sends a varlen message, consumer receives it
- Same listener, both address families, sequential

This explicitly exercises the dual-stack property that the current test matrix only covers implicitly.

## Reference Design Update

Update [2026-05-28-phase2-reference-design.md](../reference/2026-05-28-phase2-reference-design.md) P2 Socket Test Coverage checklist:

```markdown
### P2: Socket test coverage

- [x] Fixed-frame socket roundtrip tests exist
- [x] Disconnect/reconnect tests exist for both sides
- [x] `ipc::runtime` over `socket_consumer` is tested
- [ ] Dual-stack edge cases are covered on Linux and macOS
```

After this spec is implemented, the last item becomes `[x]`.

## Non-Goals

- No changes to socket implementation code (`socket_channel.cpp`)
- No new functional socket tests beyond the 3 gaps (no heartbeat, timeout, keepalive, etc.)
- No binding-layer tests (Python, Node.js, C#)
- No benchmark additions

## Success Criteria

- [ ] `FixedTcpLoopbackIPv4` passes
- [ ] `FixedBytesZeroPaddedRoundtrip` passes
- [ ] `SingleListenerServesBothIPv4AndIPv6` passes
- [ ] Phase 2 reference design P2 Socket Test Coverage checklist reflects actual state
- [ ] All 19 existing socket transport tests continue to pass
- [ ] No changes to socket implementation

## Evidence Sources

- `tests/socket_transport_test.cpp` — existing 19 socket tests
- `src/ipc/socket_channel.cpp` — `tcp_listen()` and `try_resolved_stream_socket()` dual-stack binding logic (lines ~183–290)
- `include/xproc/ipc/socket_channel.hpp` — `send_fixed_bytes` / `send_fixed_sized` declarations
- `.worktrees/ai-superpowers/docs/superpowers/reference/2026-05-28-phase2-reference-design.md` — Phase 2 checklist

## Transition Rule

After this spec is reviewed and approved, the next step is to write the implementation plan using the `writing-plans` skill, targeting a new `feat/socket-test-coverage-gap-fill` branch from `main`.
