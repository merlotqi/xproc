# xproc Python Binding

This package exposes the `xproc` C API to Python through a pybind11 module plus
the `xproc` package facade staged under `build/Python/stage`.

## Wheel Build and Install

From the repository root:

```bash
python3 -m pip install build
python3 -m build
python3 -m pip install dist/xproc-*.whl
```

`python3 -m build` uses the PyPA `build` frontend, so that command requires the
`build` package to be installed in the invoking Python environment first.

## CI Release Artifacts

GitHub Actions builds Python release artifacts in
`.github/workflows/python-wheels.yml`.

- Pull requests and pushes to `main`/`master` build wheels on Linux, macOS,
  and Windows with `cibuildwheel`.
- Each built wheel is installed and validated in CI by running
  `tests/python_wheel_smoke.py --expected-wheel {wheel}` through
  `cibuildwheel`'s test hook, which confirms the installed package comes from
  the wheel artifact rather than the source tree.
- The Linux runner also builds a source distribution with
  `python -m build --sdist`.
- The workflow uploads the produced wheels and sdist as GitHub Actions
  artifacts, and tag pushes matching `python-v*` provide the same artifact set
  for release-oriented runs.

To reproduce the release artifacts locally from the repository root:

```bash
python3 -m pip install build cibuildwheel
rm -rf wheelhouse
CIBW_BUILD="$(python3 - <<'PY'
import sys
print(f"cp{sys.version_info.major}{sys.version_info.minor}-*")
PY
)" python3 -m cibuildwheel --output-dir wheelhouse
python3 -m build --sdist
mapfile -t wheels < <(find wheelhouse -maxdepth 1 -type f -name 'xproc-*.whl' | sort)
if [ "${#wheels[@]}" -ne 1 ]; then
  printf 'expected exactly one local wheel, found %s\n' "${#wheels[@]}" >&2
  exit 1
fi
wheel_path="${wheels[0]}"
python3 -m pip install --force-reinstall "$wheel_path"
python3 tests/python_wheel_smoke.py --expected-wheel "$wheel_path"
```

`python3 -m cibuildwheel --output-dir wheelhouse` builds the same class of wheel
artifacts as CI for the current host platform. Setting `CIBW_BUILD` to the
invoking interpreter's `cpXY-*` tag keeps the local reproduction focused on the
single compatible wheel for `python3`, and the shell check aborts if the local
`wheelhouse/` contents do not match that expectation. `python3 -m build --sdist`
produces the source tarball under `dist/`. On Linux, `cibuildwheel` expects a
working Docker setup for the default manylinux build path.

## Direct CMake Staged Package Build

If you want the staged package directly from CMake instead of building a wheel,
run:

```bash
cmake -S . -B build -DXPROC_BUILD_CAPI=ON -DXPROC_BUILD_PYTHON=ON
cmake --build build --target xproc_python_package
```

## Test

```bash
cd build
ctest -L python
```

## IDE Support

The package ships a [PEP 561](https://peps.python.org/pep-0561/) style stub file
at `xproc/__init__.pyi` plus `xproc/py.typed`, so IDEs and type checkers can
pick up signatures for the pybind11 module.

## Examples

From the repository root:

```bash
python3 Python/examples/fixed_channel_inprocess.py
python3 Python/examples/varlen_channel_inprocess.py
python3 Python/examples/observer_peek_demo.py
python3 Python/examples/parent_child_struct_monitor.py
```

Cross-language example (Python parent + C++ child):

```bash
cmake --build build --target xproc_node_cpp_child_struct_writer xproc_python_package
python3 Python/examples/python_parent_cpp_child_struct_monitor.py
```

Python worker launched by the C++ parent handshake demo:

```bash
cmake --build build --target xproc_cpp_python_handshake_progress_demo xproc_python_package
./build/examples/xproc_cpp_python_handshake_progress_demo --python python3
```

If you build into a non-default directory, pass `--module-dir /path/to/Python/stage`
or set `XPROC_PYTHON_MODULE_DIR=/path/to/Python/stage`.

## What each example shows

- `examples/fixed_channel_inprocess.py`
  A single Python process opens a fixed-size producer/consumer pair, sends
  `int32` counters, and validates the receive sequence.

- `examples/varlen_channel_inprocess.py`
  A single Python process opens a varlen producer/consumer pair and exchanges
  text messages.

- `examples/observer_peek_demo.py`
  Demonstrates the read-only `Observer` API, `peek_copy()`, and `snapshot()`
  alongside a normal consumer.

- `examples/parent_child_struct_monitor.py`
  Python parent creates the SHM segment as consumer, relaunches itself as a
  child producer, and monitors fixed-size struct payloads.

- `examples/python_parent_cpp_child_struct_monitor.py`
  Python parent creates the consumer and spawns the C++ executable
  `xproc_node_cpp_child_struct_writer`, which attaches as producer and publishes
  struct payloads into the same SHM segment.

- `examples/cpp_python_handshake_worker.py`
  Worker script for the C++ launcher demo. It waits for a parent `ack` over
  xproc, then only demonstrates identity validation and progress reporting.
