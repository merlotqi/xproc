# xproc Python Binding

This package exposes the `xproc` C API to Python through a pybind11 module plus
the `xproc` package facade staged under `build/Python/stage`.

## Build

From the repository root:

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
