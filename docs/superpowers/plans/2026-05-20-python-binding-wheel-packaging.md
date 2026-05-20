# Python Binding Wheel Packaging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert the existing staged `pybind11` Python binding into a standard multi-wheel Python package build without breaking the current CMake-based in-repo development flow.

**Architecture:** Keep `Python/` as the source location for the package facade and binding code, add a root Python packaging entry point with `scikit-build-core`, and teach `Python/CMakeLists.txt` to support both the existing staged-package workflow and wheel-install layout. Validate the result through staged-package smoke coverage, installed-wheel smoke coverage, and CI wheel automation via `cibuildwheel`.

**Tech Stack:** CMake, CTest, pybind11, scikit-build-core, Python packaging (`pyproject.toml`, `build`, `pip`), cibuildwheel, GitHub Actions.

---

## Scope

This plan implements:

- root Python packaging metadata for wheel and `sdist` builds
- dual-mode Python binding CMake support for staged local builds and wheel installs
- installed-wheel smoke validation
- CI automation for multi-version CPython wheel builds
- documentation for local builds, wheel builds, and release flow

This plan does **not** implement:

- `abi3` / stable ABI packaging
- a binding technology switch away from `pybind11`
- Python API redesign
- PyPI publication automation beyond generating release artifacts
- support for PyPy

## File Structure

- Create on `ai-superpowers`: `docs/superpowers/plans/2026-05-20-python-binding-wheel-packaging.md`
- Modify on `feat/python-binding`: `pyproject.toml`
- Modify on `feat/python-binding`: `CMakeLists.txt`
- Modify on `feat/python-binding`: `Python/CMakeLists.txt`
- Modify on `feat/python-binding`: `Python/README.md`
- Modify on `feat/python-binding`: `Python/tests/smoke_test.py`
- Modify on `feat/python-binding`: `Python/xproc/__init__.py`
- Create on `feat/python-binding`: `.github/workflows/python-wheels.yml`
- Create on `feat/python-binding`: `tests/python_wheel_smoke.py`

### Task 1: Add the Root Python Packaging Entry Point

**Files:**
- Create: `pyproject.toml`
- Modify: `Python/README.md`
- Reference: `Python/CMakeLists.txt`
- Reference: `Python/xproc/__init__.py`

- [ ] **Step 1: Write the failing packaging smoke command**

Run:

```bash
cd /home/merlot/codes/xproc/.worktrees/feat-python-binding
python3 -m build
```

Expected:
- failure because the repository root does not yet provide `pyproject.toml` and a Python build backend

- [ ] **Step 2: Add root `pyproject.toml` with `scikit-build-core`**

Create `pyproject.toml` with content in this shape:

```toml
[build-system]
requires = ["scikit-build-core>=0.10", "pybind11>=2.12"]
build-backend = "scikit_build_core.build"

[project]
name = "xproc"
version = "0.2.0"
description = "Python bindings for xproc"
readme = "Python/README.md"
requires-python = ">=3.10"
authors = [{ name = "xproc contributors" }]
license = { text = "MIT" }
classifiers = [
  "Programming Language :: Python :: 3",
  "Programming Language :: Python :: 3 :: Only",
  "Programming Language :: Python :: 3.10",
  "Programming Language :: Python :: 3.11",
  "Programming Language :: Python :: 3.12",
  "Programming Language :: Python :: 3.13",
  "Programming Language :: C++",
  "Operating System :: POSIX :: Linux",
  "Operating System :: Microsoft :: Windows",
]

[tool.scikit-build]
cmake.source-dir = "."
wheel.packages = ["Python/xproc"]
sdist.include = [
  "CMakeLists.txt",
  "Python",
  "capi",
  "include",
  "src",
  "cmake",
  "LICENSE",
  "README.md",
]

[tool.scikit-build.cmake.define]
XPROC_BUILD_CAPI = "ON"
XPROC_BUILD_PYTHON = "ON"
XPROC_BUILD_NODE = "OFF"
XPROC_BUILD_EXAMPLES = "OFF"
XPROC_BUILD_BENCHMARKS = "OFF"
XPROC_BUILD_TESTS = "ON"
```

Expected:
- the repository root becomes a valid Python package build entry point
- wheel builds default to the minimum target set needed for Python packaging

- [ ] **Step 3: Document the new build entry point in `Python/README.md`**

Add a build section that includes these commands:

```bash
python3 -m build
pip install dist/xproc-*.whl
```

Also keep the existing staged-package workflow documented:

```bash
cmake -S . -B build -DXPROC_BUILD_CAPI=ON -DXPROC_BUILD_PYTHON=ON
cmake --build build --target xproc_python_package
```

Expected:
- the README explains both developer and packaging workflows without replacing one with the other

- [ ] **Step 4: Run the packaging smoke command again**

Run:

```bash
cd /home/merlot/codes/xproc/.worktrees/feat-python-binding
python3 -m build
```

Expected:
- failure shifts from “missing build backend” to a concrete CMake/install-layout issue that the next task will address

- [ ] **Step 5: Commit the packaging entry-point scaffolding**

Run:

```bash
git -C /home/merlot/codes/xproc/.worktrees/feat-python-binding add pyproject.toml Python/README.md
git -C /home/merlot/codes/xproc/.worktrees/feat-python-binding commit -m "build: add python wheel packaging entry point"
```

### Task 2: Teach CMake to Support Wheel Installs and Staged Local Builds

**Files:**
- Modify: `Python/CMakeLists.txt`
- Modify: `CMakeLists.txt`
- Reference: `Python/xproc/__init__.py`

- [ ] **Step 1: Write the failing CMake install expectation**

Run:

```bash
cd /home/merlot/codes/xproc/.worktrees/feat-python-binding
python3 -m build
```

Expected:
- failure because the current CMake logic is optimized for `xproc_python_package` staging and does not yet cleanly support wheel-oriented install layout

- [ ] **Step 2: Add an explicit install destination for the Python package**

Update `Python/CMakeLists.txt` so the package destination is overridable by wheel builds while keeping the existing site-packages fallback:

```cmake
set(XPROC_PYTHON_INSTALL_DIR "${XPROC_PYTHON_SITEARCH}/${XPROC_PYTHON_PACKAGE_NAME}" CACHE PATH
    "Install destination for the Python package")
```

Keep `XPROC_PYTHON_STAGE_ROOT` and `XPROC_PYTHON_STAGE_DIR` for the local staged-package flow.

Expected:
- staged local builds still write to `build/Python/stage/xproc`
- wheel builds gain a stable package install destination

- [ ] **Step 3: Make staged copies and installed files come from one file list**

Refactor `Python/CMakeLists.txt` so these assets are defined once and used by both the custom stage target and the install rules:

```cmake
set(XPROC_PYTHON_PACKAGE_FILES
    "${XPROC_PYTHON_SOURCE_DIR}/__init__.py"
    "${XPROC_PYTHON_SOURCE_DIR}/__init__.pyi"
    "${XPROC_PYTHON_SOURCE_DIR}/py.typed"
)
```

Keep:

```cmake
add_custom_target(xproc_python_package ALL
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${XPROC_PYTHON_STAGE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${XPROC_PYTHON_PACKAGE_FILES} "${XPROC_PYTHON_STAGE_DIR}"
    DEPENDS _xproc_pybind ${XPROC_PYTHON_PACKAGE_FILES}
)
```

and keep matching install rules:

```cmake
install(FILES ${XPROC_PYTHON_PACKAGE_FILES}
    DESTINATION "${XPROC_PYTHON_INSTALL_DIR}")
```

Expected:
- staged builds and install builds remain aligned on package contents

- [ ] **Step 4: Ensure native library placement works for both build modes**

Keep `_xproc_pybind` output directed at the stage package for local builds:

```cmake
set_target_properties(_xproc_pybind PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${XPROC_PYTHON_STAGE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${XPROC_PYTHON_STAGE_DIR}"
)
```

and ensure install rules package all required native artifacts:

```cmake
install(TARGETS _xproc_pybind
    LIBRARY DESTINATION "${XPROC_PYTHON_INSTALL_DIR}"
    RUNTIME DESTINATION "${XPROC_PYTHON_INSTALL_DIR}")
```

For shared-library builds, preserve:

```cmake
install(TARGETS xproc xproc_c
    LIBRARY DESTINATION "${XPROC_PYTHON_INSTALL_DIR}"
    RUNTIME DESTINATION "${XPROC_PYTHON_INSTALL_DIR}")
```

Expected:
- local stage layout continues to work
- installed wheels carry the extension module and any needed shared libraries

- [ ] **Step 5: Keep top-level CMake usable for Python-only packaging**

Verify root `CMakeLists.txt` continues to honor:

```cmake
option(XPROC_BUILD_EXAMPLES "Build examples" ON)
option(XPROC_BUILD_BENCHMARKS "Build benchmarks with Google Benchmark" ON)
option(XPROC_BUILD_NODE "Build JavaScript binding" ON)
```

while allowing wheel builds to force them off via `pyproject.toml`.

If needed, add a short comment near these options explaining that Python wheel builds intentionally disable unrelated subprojects through the build backend defines.

Expected:
- the regular repository build remains unchanged
- Python wheel builds avoid unrelated dependencies such as Node, benchmarks, and examples

- [ ] **Step 6: Re-run the staged local build**

Run:

```bash
cd /home/merlot/codes/xproc/.worktrees/feat-python-binding
cmake -S . -B build -DXPROC_BUILD_CAPI=ON -DXPROC_BUILD_PYTHON=ON -DXPROC_BUILD_EXAMPLES=OFF -DXPROC_BUILD_BENCHMARKS=OFF -DXPROC_BUILD_NODE=OFF -DXPROC_BUILD_TESTS=ON
cmake --build build --target xproc_python_package -j4
```

Expected:
- the existing staged package still builds successfully

- [ ] **Step 7: Re-run the wheel build**

Run:

```bash
cd /home/merlot/codes/xproc/.worktrees/feat-python-binding
python3 -m build
```

Expected:
- both a wheel and an `sdist` are generated under `dist/`

- [ ] **Step 8: Commit the dual-mode CMake work**

Run:

```bash
git -C /home/merlot/codes/xproc/.worktrees/feat-python-binding add CMakeLists.txt Python/CMakeLists.txt
git -C /home/merlot/codes/xproc/.worktrees/feat-python-binding commit -m "build: support python wheel install layout"
```

### Task 3: Add Installed-Package Smoke Coverage

**Files:**
- Modify: `Python/tests/smoke_test.py`
- Create: `tests/python_wheel_smoke.py`
- Modify: `Python/xproc/__init__.py`

- [ ] **Step 1: Write the failing installed-wheel smoke command**

Run:

```bash
cd /home/merlot/codes/xproc/.worktrees/feat-python-binding
python3 -m venv .venv-wheel-test
. .venv-wheel-test/bin/activate
pip install dist/xproc-*.whl
python tests/python_wheel_smoke.py
```

Expected:
- failure because there is not yet a dedicated installed-wheel smoke entry point

- [ ] **Step 2: Keep the package import surface minimal and explicit**

Verify `Python/xproc/__init__.py` remains:

```python
from ._xproc_pybind import *  # noqa: F401,F403
```

Only change it if the wheel smoke validation exposes an import-path issue.

Expected:
- the package facade stays minimal
- import issues are fixed at packaging/layout level rather than by adding ad hoc import hacks

- [ ] **Step 3: Add a dedicated installed-wheel smoke script**

Create `tests/python_wheel_smoke.py` in this shape:

```python
from __future__ import annotations

import xproc


def main() -> int:
    assert xproc.version_string()
    assert xproc.status_string(xproc.Status.OK) == "ok"
    defaults = xproc.TransportOptions()
    assert defaults.creator_timestamp_ns == 0
    assert defaults.creator_flags == 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

Expected:
- there is a smoke entry point that validates the installed wheel without requiring `--module-dir`

- [ ] **Step 4: Keep `Python/tests/smoke_test.py` focused on staged local builds**

Preserve the existing stage-based behavior:

```python
parser.add_argument("--module-dir", required=True)
sys.path.insert(0, str(Path(args.module_dir).resolve()))
```

If needed, factor the shared assertions into a helper function so the same behavioral checks can be reused by both smoke paths.

Expected:
- the staged-package test still validates the developer build
- the installed-wheel test validates the distributable artifact

- [ ] **Step 5: Run both smoke paths**

Run:

```bash
cd /home/merlot/codes/xproc/.worktrees/feat-python-binding
ctest --test-dir build -R xproc_python_smoke --output-on-failure
python3 -m venv .venv-wheel-test
. .venv-wheel-test/bin/activate
pip install --upgrade pip
pip install dist/xproc-*.whl
python tests/python_wheel_smoke.py
```

Expected:
- staged-package smoke passes
- installed-wheel smoke passes

- [ ] **Step 6: Commit the smoke coverage work**

Run:

```bash
git -C /home/merlot/codes/xproc/.worktrees/feat-python-binding add Python/tests/smoke_test.py Python/xproc/__init__.py tests/python_wheel_smoke.py
git -C /home/merlot/codes/xproc/.worktrees/feat-python-binding commit -m "test: add installed python wheel smoke coverage"
```

### Task 4: Add CI Wheel Automation

**Files:**
- Create: `.github/workflows/python-wheels.yml`
- Modify: `Python/README.md`
- Reference: existing GitHub workflows under `.github/workflows/`

- [ ] **Step 1: Write the failing CI dry-run expectation**

Run:

```bash
cd /home/merlot/codes/xproc/.worktrees/feat-python-binding
ls .github/workflows
```

Expected:
- no dedicated Python wheel workflow exists yet

- [ ] **Step 2: Add a dedicated `cibuildwheel` workflow**

Create `.github/workflows/python-wheels.yml` with a matrix in this shape:

```yaml
name: python-wheels

on:
  workflow_dispatch:
  push:
    branches: [main]
    tags: ["v*"]
  pull_request:

jobs:
  build:
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest, windows-latest]
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: "3.12"
      - run: python -m pip install --upgrade pip cibuildwheel build
      - run: python -m cibuildwheel --output-dir wheelhouse
        env:
          CIBW_BUILD: "cp310-* cp311-* cp312-* cp313-*"
          CIBW_SKIP: "*-musllinux_* pp*"
          CIBW_TEST_COMMAND: "python {project}/tests/python_wheel_smoke.py"
      - uses: actions/upload-artifact@v4
        with:
          name: wheels-${{ matrix.os }}
          path: wheelhouse/*.whl
```

Add a separate source-dist step on one platform:

```yaml
      - if: matrix.os == 'ubuntu-latest'
        run: python -m build --sdist
```

Expected:
- CI can build and test wheels across the supported CPython matrix
- CI also produces an `sdist`

- [ ] **Step 3: Document the release artifact workflow**

Add a short release section to `Python/README.md` that includes:

```bash
python3 -m build
python3 -m cibuildwheel --output-dir wheelhouse
```

Expected:
- developers can reproduce the CI artifact flow locally

- [ ] **Step 4: Commit the CI automation work**

Run:

```bash
git -C /home/merlot/codes/xproc/.worktrees/feat-python-binding add .github/workflows/python-wheels.yml Python/README.md
git -C /home/merlot/codes/xproc/.worktrees/feat-python-binding commit -m "ci: add python wheel build workflow"
```

### Task 5: Run End-to-End Verification

**Files:**
- Reference: `pyproject.toml`
- Reference: `Python/CMakeLists.txt`
- Reference: `Python/tests/smoke_test.py`
- Reference: `tests/python_wheel_smoke.py`
- Reference: `.github/workflows/python-wheels.yml`

- [ ] **Step 1: Run the staged local build verification**

Run:

```bash
cd /home/merlot/codes/xproc/.worktrees/feat-python-binding
cmake -S . -B build -DXPROC_BUILD_CAPI=ON -DXPROC_BUILD_PYTHON=ON -DXPROC_BUILD_EXAMPLES=OFF -DXPROC_BUILD_BENCHMARKS=OFF -DXPROC_BUILD_NODE=OFF -DXPROC_BUILD_TESTS=ON
cmake --build build --target xproc_python_package -j4
ctest --test-dir build -R xproc_python_smoke --output-on-failure
```

Expected:
- the in-repo staged developer workflow still works

- [ ] **Step 2: Run the wheel build verification**

Run:

```bash
cd /home/merlot/codes/xproc/.worktrees/feat-python-binding
rm -rf dist
python3 -m build
ls dist
```

Expected:
- `dist/` contains one wheel for the active interpreter and one `sdist`

- [ ] **Step 3: Run the installed-wheel verification**

Run:

```bash
cd /home/merlot/codes/xproc/.worktrees/feat-python-binding
rm -rf .venv-wheel-test
python3 -m venv .venv-wheel-test
. .venv-wheel-test/bin/activate
pip install --upgrade pip
pip install dist/xproc-*.whl
python tests/python_wheel_smoke.py
```

Expected:
- the installed wheel imports and executes its minimum smoke checks successfully

- [ ] **Step 4: Sanity-check the source distribution**

Run:

```bash
cd /home/merlot/codes/xproc/.worktrees/feat-python-binding
python3 -m pip install --upgrade build
python3 -m build --sdist
tar -tf dist/xproc-*.tar.gz | sed -n '1,80p'
```

Expected:
- the `sdist` exists
- the archive includes the source directories required to build the Python package

- [ ] **Step 5: Commit the final verification pass**

Run:

```bash
git -C /home/merlot/codes/xproc/.worktrees/feat-python-binding status --short
```

Expected:
- no uncommitted verification-only changes remain

## Success Criteria

- `ai-superpowers` contains this implementation plan and no product-code edits
- `feat/python-binding` can build the staged local Python package through CMake
- `feat/python-binding` can produce a wheel and `sdist` from the repository root
- the installed wheel passes a dedicated smoke check in a clean virtual environment
- CI can build and test CPython wheels across the supported platform matrix
