# xproc Python Binding Wheel Packaging Design

## Scope

This design only addresses Python binding build and distribution. It does not change the semantics of `xproc`, `xproc_c`, or the current public `pybind11` API surface.

The deliverables for this iteration are:

- Keep the current `pybind11`-based binding implementation
- Upgrade from an in-repo staged package to standard Python wheel distribution
- Build one wheel per supported CPython minor version
- Keep `sdist` as a fallback for platforms without prebuilt wheels
- Preserve the existing CMake-based local development and `ctest` validation flow

This iteration explicitly does not include:

- `abi3` or limited API work
- A switch to `cffi`, `ctypes`, or another binding technology
- Python API redesign
- Additional runtime third-party dependencies

## Problem Statement

The current Python binding lives under `Python/` and builds a CPython extension module directly via `find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)` and `pybind11_add_module(_xproc_pybind ...)`.

That model works well for local development, but the build artifacts are inherently tied to the Python minor version selected at compile time:

- An extension built against Python 3.11 cannot be reliably imported from Python 3.12
- The current `xproc_python_package` target is optimized for in-repo staging rather than standard wheel publication
- Publishing to PyPI or GitHub Releases would require extra packaging glue to gather and wrap the staged artifacts

Because the `xproc` Python binding is small and has no additional runtime third-party dependencies, publishing multiple wheels is an acceptable cost. The most appropriate direction is therefore to keep the current binding implementation and change the distribution model rather than chasing a single cross-version binary.

## Decision

Adopt the combination of `scikit-build-core + CMake + pybind11 + cibuildwheel`:

- `scikit-build-core` becomes the Python build frontend
- Existing CMake continues to compile `_xproc_pybind` and any required native libraries
- Each supported CPython minor version gets its own wheel
- CI uses `cibuildwheel` to produce Linux, macOS, and Windows wheels
- Releases also include an `sdist`

This decision is driven by four things:

- It fits the current project structure with the least migration cost
- It avoids shrinking binding capabilities for `abi3`
- It gives Python users a standard install experience, where `pip` selects the matching wheel automatically
- It preserves the existing in-repo CMake testing entry points instead of forcing development onto a Python-only toolchain

## Architecture

### Packaging Entry Point

Add a root-level `pyproject.toml` to standardize Python package builds. The Python distribution metadata should be rooted at the repository top level, while the Python sources remain under `Python/`:

- Python package sources: `Python/xproc/`
- Binding sources: `Python/src/python_binding.cpp`
- Python documentation: `Python/README.md`
- Build metadata: root `pyproject.toml`

When `python -m build` or `pip wheel .` runs, the build frontend should invoke CMake using the current interpreter and build only the extension module compatible with that interpreter.

### CMake Responsibilities

`Python/CMakeLists.txt` should continue to own:

- Resolving the active Python interpreter and development headers
- Building `_xproc_pybind`
- Linking `xproc_c`, and `xproc` when required
- Producing the staged package used by local development
- Registering the `ctest` smoke test

For wheel builds, `Python/CMakeLists.txt` also needs to support a standard install flow into the wheel staging area instead of relying only on the custom `xproc_python_package` target.

### Native Library Strategy

The wheel should include every native artifact required to run the Python package, so users do not need a separate installation of `xproc` or `xproc_c` shared libraries.

Recommended strategy:

- If `xproc` and `xproc_c` are shared libraries, package them alongside `_xproc_pybind` inside the `xproc/` package directory
- If they are static libraries, continue to link them into the extension module or an internal intermediate target and do not expose additional shared objects

This ensures that `import xproc` works immediately after installation and does not depend on system library search paths.

## Build Flow

### Local Development

Keep the current CMake-driven development flow:

- `cmake -S . -B build -DXPROC_BUILD_CAPI=ON -DXPROC_BUILD_PYTHON=ON`
- `cmake --build build --target xproc_python_package`
- `ctest --test-dir build -L python`

This remains the primary flow for repository developers and cross-language local integration work.

### Wheel Build

Add a standard Python build flow:

- `python -m build`
- or `pip wheel .`

That flow should:

- Use the current interpreter to resolve the build backend
- Invoke CMake to build the matching `_xproc_pybind`
- Collect `Python/xproc/__init__.py`, `__init__.pyi`, and `py.typed`
- Collect the extension module and any required native libraries
- Emit a wheel tagged for the active CPython version

### CI Distribution

Use `cibuildwheel` in CI to build wheels across the supported platform and Python version matrix. The first phase should target CPython only and exclude PyPy.

Recommended initial support matrix:

- Linux: `x86_64`, `aarch64`
- macOS: `x86_64`, `arm64`
- Windows: `AMD64`
- Python: 3.10, 3.11, 3.12, 3.13

Build and publish one `sdist` alongside the wheels so unsupported platforms can still fall back to a source build.

## Repository Changes

The first implementation phase should add or adjust the following boundaries:

- Add root `pyproject.toml`
- Add any root-level packaging references or Python build notes if needed
- Adjust `Python/CMakeLists.txt` to support both wheel builds and the current staged-package flow
- Add any helper packaging configuration needed for package data collection
- Add a CI workflow for `cibuildwheel` builds and artifact upload
- Update `Python/README.md` with `pip wheel`, `python -m build`, and installation guidance

These changes must not break the existing:

- `XPROC_BUILD_PYTHON`
- `xproc_python_package`
- `xproc_python_smoke`
- Python example import-path expectations

## Error Handling And Compatibility

The design should explicitly account for the following compatibility behavior:

- Python version mismatches are no longer solved by reusing one local build artifact across versions; they are solved by installing a matching wheel
- Wheel build failures should surface as standard Python build backend failures rather than silently falling back to the staged package directory
- On platforms without a matching wheel, `sdist` builds should still fail clearly when prerequisites are missing, such as a compiler, CMake, or Python headers
- `Python/tests/smoke_test.py` should be able to validate both the staged local build path and an installed wheel path

## Testing

Validation should happen at three layers.

### 1. Existing CMake Smoke Coverage

Continue running:

- `ctest --test-dir build -L python`

This protects the in-repo development path from regressions.

### 2. Wheel Import Smoke

Add wheel installation validation:

- Build the wheel
- Install it into a clean virtual environment
- Run a minimal import test and a basic roundtrip smoke test

The goal is to verify:

- `import xproc` succeeds
- `_xproc_pybind` loads correctly
- Required shared libraries are resolvable
- Existing fixed and varlen happy paths still work

### 3. sdist Fallback

At least one CI platform should validate the `sdist` install path so source distribution support is real rather than nominal.

## Migration Plan

Implement the change in two phases.

### Phase 1: Packaging Foundation

- Add `pyproject.toml`
- Make `scikit-build-core` capable of driving the current CMake build into a wheel
- Keep `xproc_python_package` and `ctest` working
- Verify local wheel build and installation successfully

### Phase 2: CI And Release

- Add `cibuildwheel`
- Bring up the multi-platform, multi-Python-version matrix
- Add wheel smoke tests and `sdist` smoke tests
- Document the release workflow

## Resolved Questions

This design locks in the following decisions:

- Do not pursue `abi3`
- Do not switch binding technologies
- Accept the cost of publishing multiple wheels
- Bundle required native libraries inside the Python wheel
- Keep the current CMake staged-package flow as the primary in-repo developer entry point

## Success Criteria

The work is complete when all of the following are true:

- Developers can still build and test the Python binding through the existing CMake targets
- `python -m build` can produce a valid wheel and `sdist` from the repository root
- Each supported Python minor version produces its own matching wheel
- Installing a wheel is sufficient to make `import xproc` work with no extra manual setup
- CI can automatically build and validate Python wheels for all supported target platforms
