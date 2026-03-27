# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project aims to follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Shared-library build option (`XPROC_BUILD_SHARED`) and install updates.
- pkg-config generation/install support (`xproc.pc`).
- Docker-based CI workflow.
- Benchmark target split with `xproc_run_benchmarks`.
- Sphinx-compatible RST documentation under `docs/`.
- Project support files (`SECURITY.md`, `CONTRIBUTING.md`, `CODEOWNERS`).

### Changed

- Benchmark layout and execution model for better suite-level control.

### Fixed

- Benchmark crash paths related to invalid benchmark argument usage and ringbuffer benchmark stability.

## [0.2.0] - 2026-03-26

### Added

- Core xproc IPC/ringbuffer APIs, tests, examples, and CI foundation.

