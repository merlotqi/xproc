# xproc Node API Usability and npm Publishing Design

Date: 2026-04-26
Branch: `channel-manifest-phase-1`
AI-doc branch: `ai-superpowers`
Status: Proposed and user-approved in interactive design review

## Summary

This design defines the next-step work for the `node/` module on the `channel-manifest-phase-1` branch, with two priorities:

1. Make the Node API easier to use for the common shared-memory and socket workflows.
2. Make `npm install xproc` work out of the box on mainstream platforms by shipping prebuilt native binaries.

The recommended direction is an incremental design:

- Keep the existing low-level `Producer` / `Consumer` / `Observer` API as the stable raw layer.
- Add a new high-level `shm` and `socket` API for happy-path usage.
- Publish a single `xproc` npm package that contains package-local prebuilt binaries under `prebuilds/`.
- Load those binaries at runtime with `node-gyp-build`, while preserving a development fallback for local repository builds.

This approach improves first-run usability without introducing a breaking change or forcing an early package split.

## Current State

The current branch already has a meaningful Phase 1 Node binding:

- Native `Producer`, `Consumer`, and `Observer` bindings exist.
- A JavaScript facade in `node/index.js` normalizes options and byte inputs.
- TypeScript declarations exist in `node/index.d.ts`.
- Node examples and smoke tests exist and cover manifest-backed shared-memory behavior.
- The binding can round-trip persisted creator metadata once the native addon is rebuilt from current sources.

The current branch also has several gaps that matter for external npm users:

- The public Node API is still oriented around low-level `TransportOptions` rather than task-oriented workflows.
- The package README is written from a repository build perspective rather than an npm consumer perspective.
- `npm pack --dry-run` currently includes source and test files but does not include installable native artifacts.
- Installing the current tarball in a clean directory fails at `require("xproc")` because no `.node` binary is present in the package and no install-time build path is wired up.
- Local repository development can observe stale build artifacts if JavaScript or TypeScript changes are made without rebuilding `build/node/xproc.node`.

In short, the binding itself is viable, but the package is not yet publish-ready and the happy path is not yet ergonomic enough.

## Goals

- Make `npm install xproc` directly usable on:
  - Linux x64 glibc
  - Linux arm64 glibc
  - macOS x64
  - macOS arm64
  - Windows x64
- Preserve the current low-level API for power users and compatibility.
- Add high-level APIs for both shared-memory and socket workflows.
- Keep the design compatible with the existing C API and manifest-backed Phase 1 behavior.
- Ensure packaging and CI verify the real npm-install experience, not only in-repo builds.

## Non-Goals

- Support musl / Alpine in this phase.
- Split the package into per-platform npm packages in this phase.
- Redesign Python or C binding ergonomics as part of this work.
- Introduce breaking API removals for current Node consumers.
- Add ESM/CJS dual-package complexity in this phase.

## User-Facing Problems To Solve

### Problem 1: Shared-memory happy path is too low-level

Today, users still need to understand and manually compose fields such as `backend`, `channelType`, `createIfMissing`, `shmSize`, and sometimes `itemSize` even for simple create-or-attach flows.

### Problem 2: Socket happy path exposes transport plumbing directly

Users should think in terms of `listen` and `connect`, not `socketListen` plus raw transport fields.

### Problem 3: npm package is not independently installable

A package consumer should not need a repository checkout, a pre-existing `build/` tree, or a manual CMake workflow just to `require("xproc")`.

### Problem 4: Development and release loading paths are conflated

The current loader searches local repository build locations. That is useful during development, but it is not a publishable runtime story by itself.

## Options Considered

### Option A: Incremental high-level API plus preserved raw API

- Keep `new Producer(options)`, `new Consumer(options)`, and `new Observer(options)`.
- Add `xproc.shm.*` and `xproc.socket.*` helper APIs above them.
- Publish a single package with package-local prebuilds.

Pros:

- Lowest disruption to existing work.
- Fits current source layout well.
- Fastest path to a usable npm release.
- Lets README and examples move to the new ergonomic API while raw access remains available.

Cons:

- Two API layers coexist.
- Some naming duplication will remain for advanced users.

### Option B: Fluent builder-style Node API

- Mirror the C++ builder style more closely with chained builders.

Pros:

- Strong conceptual parity with C++ builders.
- Expressive for advanced composition.

Cons:

- Heavier typing and documentation burden.
- Slower path to a publishable release.
- Less idiomatic for many Node users than simple object-shaped helpers.

### Option C: High-level-only default API with raw API moved aside

- Make the top-level package mostly expose high-level helpers and move raw APIs to a subpath.

Pros:

- Cleanest experience for new users.

Cons:

- Higher breaking-change risk.
- Premature before the package is even installable from npm.
- More likely to require follow-up migration work.

## Decision

Choose Option A.

This design optimizes for immediate usability and release-readiness while preserving the current low-level surface. It aligns with the current codebase structure and minimizes risk during the first npm publication phase.

## Proposed Module Architecture

The Node package will expose three layers.

### 1. Native loading layer

Responsibility:

- Load the correct native `.node` artifact for published packages.
- Preserve local development fallback behavior for repository builds.

Behavior:

- In published packages, prefer `node-gyp-build` to resolve a matching binary from `prebuilds/`.
- In local development, retain fallback resolution for repository build outputs such as `build/node/xproc.node`.
- Produce a clear load failure that distinguishes:
  - no compatible packaged binary found
  - packaged binary found but failed to load
  - local development fallback not built yet

### 2. Raw binding layer

Responsibility:

- Continue exposing the current low-level primitives:
  - `Producer`
  - `Consumer`
  - `Observer`
  - `validateOptionsFor`
  - status/layout constants
  - error metadata

Behavior:

- No breaking changes to constructor-based raw usage in this phase.
- Existing normalization helpers remain internal support for the raw layer and for the new high-level APIs.

### 3. High-level ergonomic layer

Responsibility:

- Expose simple task-oriented entry points for shared memory and sockets.

Behavior:

- New top-level namespaces:
  - `xproc.shm`
  - `xproc.socket`
- High-level helpers translate ergonomic inputs into the existing raw transport options and then reuse the raw binding layer.

## Proposed High-Level API

### Shared Memory API

#### Fixed channel creation

```ts
const created = xproc.shm.createFixedChannel({
  path: "/demo",
  itemSize: 4,
  dataCapacity: 16384,
  schemaId: 0x1234n,
});
```

Returns a lightweight endpoint bundle with:

- `options()`
- `openProducer()`
- `openConsumer()`
- `openObserver()`

#### Fixed channel attach

```ts
const attached = xproc.shm.attachFixedChannel({
  path: "/demo",
  schemaId: 0x1234n,
});

const consumer = attached.openConsumer();
```

#### Variable-length channel creation

```ts
const created = xproc.shm.createVarlenChannel({
  path: "/messages",
  dataCapacity: 32768,
});
```

#### Variable-length channel attach

```ts
const attached = xproc.shm.attachVarlenChannel({
  path: "/messages",
});

const observer = attached.openObserver();
```

#### Shared-memory API rules

- Creation APIs accept `dataCapacity`, not `shmSize`.
- Attach APIs infer existing layout and do not require callers to restate fixed `itemSize`.
- High-level SHM APIs internally set:
  - `backend = shared memory`
  - `createIfMissing = true` for create flows
  - `createIfMissing = false` for attach flows
  - `channelType` based on the called helper
- Optional advanced metadata such as `schemaId`, `dataAlign`, `creatorTimestampNs`, `creatorFlags`, and `win32ObjectNamespace` remain available where they make sense.

### Socket API

#### Fixed listener

```ts
const listener = xproc.socket.listenFixed({
  host: "::",
  port: 9000,
  itemSize: 64,
});

const consumer = listener.openConsumer();
```

#### Fixed connector

```ts
const peer = xproc.socket.connectFixed({
  host: "127.0.0.1",
  port: 9000,
  itemSize: 64,
});

const producer = peer.openProducer();
```

#### Variable-length listener and connector

```ts
const server = xproc.socket.listenVarlen({
  host: "::",
  port: 9001,
});

const client = xproc.socket.connectVarlen({
  host: "127.0.0.1",
  port: 9001,
});
```

#### Socket API rules

- Socket high-level APIs are named in transport-native terms: `listen*` and `connect*`.
- High-level socket APIs internally set:
  - `backend = socket`
  - `socketListen = true` for listeners
  - `socketListen = false` for connectors
  - `channelType` based on the called helper
- Retry-related inputs remain available via explicit high-level fields.

## Type Design

High-level APIs will use dedicated input types instead of re-exporting raw `TransportOptions` unchanged.

### Shared memory create inputs

- `path`
- `itemSize` for fixed only
- `dataCapacity`
- optional `schemaId`
- optional `dataAlign`
- optional `creatorTimestampNs`
- optional `creatorFlags`
- optional `win32ObjectNamespace`

### Shared memory attach inputs

- `path`
- optional `schemaId`
- optional `win32ObjectNamespace`

### Socket listen/connect inputs

- `host`
- `port`
- `itemSize` for fixed only
- optional `connectRetries`
- optional `connectRetryMs`

These types intentionally hide low-level knobs that should not be part of the happy path:

- `backend`
- `channelType`
- `createIfMissing`
- `socketListen`
- `shmSize`

The raw API remains available when callers need the full transport surface.

## Error Handling

This phase keeps a single error model across raw and high-level APIs.

- Continue throwing `Error` objects enriched with:
  - `status`
  - `statusCode`
  - `layoutError`
  - `layoutErrorCode`
- High-level helpers may add clearer operation context in error messages, such as:
  - `shm.createFixedChannel`
  - `shm.attachVarlenChannel`
  - `socket.listenFixed`
  - `socket.connectVarlen`
- Do not introduce a second incompatible error hierarchy in this phase.

This keeps diagnostics aligned with the C API and existing test expectations.

## Packaging Design

### Package structure

The first npm-release design uses a single package: `xproc`.

The published package will contain:

- runtime JavaScript files
- TypeScript declaration files
- README and LICENSE
- package-local `prebuilds/`

The package will not include unnecessary repository-only files such as:

- native C++ source files
- Node tests
- TypeScript project config files
- development-only build metadata

This will be enforced through an explicit `files` whitelist in `node/package.json`.

### Binary strategy

- Build platform-specific precompiled `.node` artifacts.
- Store them under `prebuilds/` in the package.
- Use `node-gyp-build` for runtime resolution.

Because the addon is built on Node-API, the binary distribution strategy can target ABI-stable N-API compatibility instead of publishing a separate artifact for every supported Node runtime minor.

## CI and Release Flow

### Regular CI

Regular CI continues to verify repository builds:

- install Node dependencies
- configure CMake
- build `xproc_node`
- run Node typecheck
- run Node smoke tests

### Release CI

A dedicated release workflow will:

1. Build precompiled binaries for:
   - Linux x64 glibc
   - Linux arm64 glibc
   - macOS x64
   - macOS arm64
   - Windows x64
2. Stage those outputs into package-local `prebuilds/`.
3. Run `npm pack`.
4. Install the packed tarball in a clean temporary project.
5. Verify `require("xproc")` succeeds without any repository `build/` tree.
6. Run at least one smoke-level runtime check from the installed tarball.
7. Publish to npm only after the package-level verification passes.

### Publishing model

- First release shape: single package with bundled prebuilds.
- Future evolution: platform subpackages remain possible, but are explicitly deferred until package size or release complexity becomes a real problem.

## Documentation Changes

The Node README should be reorganized around npm consumers first.

### README priorities

- Installation via npm
- Supported prebuilt platforms
- First-run shared-memory example using `xproc.shm.createFixedChannel`
- First-run socket example using `xproc.socket.listenFixed` / `connectFixed`
- Troubleshooting section for unsupported platforms and development builds
- Advanced/raw API section for `Producer` / `Consumer` / `Observer`

Repository build instructions should remain available, but should move into a contributor or development-oriented section.

## Testing Strategy

This work should add or update coverage in four areas.

### 1. High-level API behavior

- SHM fixed create/attach happy path
- SHM varlen create/attach happy path
- socket fixed listen/connect happy path
- socket varlen listen/connect happy path

### 2. Error and validation behavior

- schema mismatch surfaces through high-level SHM attach helpers
- invalid socket inputs still surface raw status/error metadata
- unsupported/missing binary load failure has a clear message

### 3. Packaging verification

- `npm pack` contains required runtime files and prebuilds
- clean install can `require("xproc")`
- clean install does not rely on repository-local `build/` outputs

### 4. Development fallback behavior

- in-repository development still works when local `build/node/xproc.node` exists
- fallback error remains actionable when the local addon is stale or missing

## Implementation Boundaries

### In scope

1. Add high-level SHM and socket APIs.
2. Add types and examples for those APIs.
3. Switch package loading to prefer bundled prebuilds.
4. Add packaging metadata so published contents are intentional.
5. Add CI/release automation for prebuilt binaries and npm package verification.
6. Update README toward npm-consumer workflows.

### Out of scope

- musl / Alpine support
- platform split packages
- Python/C parity refactor
- large raw API redesign
- ESM packaging changes

## Risks and Mitigations

### Risk: API duplication causes confusion

Mitigation:

- Make README and examples prefer the high-level APIs.
- Document raw constructors as advanced usage.

### Risk: Prebuild matrix becomes fragile

Mitigation:

- Keep the first matrix to the approved mainstream set only.
- Verify the tarball itself, not just the local build outputs.

### Risk: Development loader and published loader drift apart

Mitigation:

- Keep both paths in one loader module with explicit priority rules.
- Add tests that exercise both packaged and development scenarios.

### Risk: Scope expands into a broader bindings redesign

Mitigation:

- Treat this effort as a Node-focused usability and distribution pass only.
- Defer cross-binding parity work to later plans.

## Rollout Order

1. Refactor the loader to support packaged prebuilds plus development fallback.
2. Add the new high-level TypeScript and JavaScript API surface.
3. Update examples and README to use the new happy-path APIs.
4. Add package metadata and packaging tests.
5. Add prebuild CI and release automation.
6. Validate with `npm pack` and clean-install smoke coverage.

## Success Criteria

This work is complete when all of the following are true:

- A user can run `npm install xproc` on the approved mainstream platforms and immediately `require("xproc")`.
- The README leads with high-level SHM and socket workflows.
- The package exports both raw and high-level APIs without breaking the existing low-level constructors.
- CI proves that the packed tarball is usable outside the repository.
- The design still leaves room for later package splitting if needed, without requiring it now.

