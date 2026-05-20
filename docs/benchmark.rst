IPC Benchmark Comparisons
=========================

This document compares IPC benchmark results across **xproc**, **Qt**,
**Poco**, and selected OS-native transport alternatives. The goal is to keep
the shared-memory baseline fair at the raw mapping and handoff layer, then
show where `xproc` native channels and OS-level stream transports land
relative to that baseline.

Methodology
-----------

The primary comparison uses a fixed layout in shared memory:

- `state`: `0` means empty, `1` means payload ready
- `length`: payload size in bytes
- `payload`: raw message bytes

For the fair shared-memory baseline, all frameworks use their shared-memory
primitive only:

- `xproc`: raw `xproc::core::shm`
- `Qt`: `QSharedMemory`
- `Poco`: `Poco::SharedMemory`

The benchmark writes through one endpoint mapping and reads through a second
endpoint mapping of the same segment inside the same benchmark process. This
keeps the benchmark reproducible under Google Benchmark while still exercising
framework-specific shared-memory APIs.

OS IPC alternatives are reported separately because they are kernel-managed
stream transports rather than shared-memory mappings:

- **Windows**: named pipe (`CreateNamedPipe` + `CreateFile`)
- **Linux / macOS**: Unix domain socket (`socketpair(AF_UNIX, SOCK_STREAM)`)

`xproc` native channel results are reported separately because they measure the
library's higher-level transport rather than the raw slot protocol.

Build
-----

.. code-block:: bash

   cmake -S . -B build -DXPROC_BUILD_BENCHMARKS=ON
   cmake --build build --target xproc_bench_ipc_cross_framework

   # Optional Qt support
   cmake -S . -B build-qt -DXPROC_BUILD_BENCHMARKS=ON -DXPROC_BENCH_QT=ON
   cmake --build build-qt --target xproc_bench_ipc_cross_framework

   # Optional Poco support
   cmake -S . -B build-poco -DXPROC_BUILD_BENCHMARKS=ON -DXPROC_BENCH_POCO=ON
   cmake --build build-poco --target xproc_bench_ipc_cross_framework

Generate the Table
------------------

.. code-block:: bash

   cmake -S . -B build -DXPROC_BUILD_BENCHMARKS=ON
   cmake --build build --target xproc_generate_ipc_benchmark_docs

This target writes benchmark JSON to `build/ipc-cross-framework.json` and
regenerates the documentation fragment at `docs/_generated/ipc_benchmark_table.rst`.

Windows Sync Wait Counters
--------------------------

On Windows, `xproc_bench_sync` now reports per-operation wait counters from
`atomic_wait_win32` so tuning changes (spin/yield/timeout) can be compared
without guessing from total latency alone.

Generate the Windows table:

.. code-block:: powershell

   cmake -S . -B build -DXPROC_BUILD_BENCHMARKS=ON
   cmake --build build --target xproc_generate_sync_benchmark_docs

The target writes benchmark JSON to `build/sync-win32-wait.json` and refreshes
`docs/_generated/sync_win32_wait_table.rst`.

.. include:: _generated/sync_win32_wait_table.rst

Generated Benchmark Table
-------------------------

.. include:: _generated/ipc_benchmark_table.rst

Notes
-----

- Missing frameworks in the generated table mean the dependency was not enabled,
  not supported on the current platform, or not available for that local build.
- Results are machine-dependent and should be interpreted comparatively, not as
  universal constants.
