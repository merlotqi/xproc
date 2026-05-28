Generated Results
-----------------

.. note::

   The table below is generated from the most recent local benchmark run.
   Host: PC-MERLOT.
   Run date: 2026-04-30T14:30:04+08:00.
   Google Benchmark detected 28 CPUs at roughly 3418 MHz.
   Missing frameworks mean the corresponding optional dependency was not
   enabled, not supported on the current platform, or not available when
   the benchmark executable was built.
   OS IPC rows are platform-specific: Windows uses named pipes, while
   Linux and macOS use Unix domain sockets when that benchmark is built.

Fair Shared-Memory Baseline
^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 24 14 18

   * - Framework
     - Payload
     - Latency
   * - Poco
     - 64 B
     - 11.283 ns
   * - Qt
     - 64 B
     - 11.584 ns
   * - xproc
     - 64 B
     - 3.370 ns
   * - Poco
     - 1024 B
     - 17.559 ns
   * - Qt
     - 1024 B
     - 17.746 ns
   * - xproc
     - 1024 B
     - 12.617 ns
   * - Poco
     - 4096 B
     - 43.755 ns
   * - Qt
     - 4096 B
     - 44.120 ns
   * - xproc
     - 4096 B
     - 43.372 ns

OS IPC Alternatives
^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 24 14 18

   * - Framework
     - Payload
     - Latency
   * - Windows named pipe
     - 64 B
     - 414.854 ns
   * - Windows named pipe
     - 1024 B
     - 408.887 ns
   * - Windows named pipe
     - 4096 B
     - 881.482 ns

xproc Native Channel
^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 24 14 18

   * - Framework
     - Payload
     - Latency
   * - xproc fixed
     - 64 B
     - 17.416 ns
   * - xproc varlen
     - 64 B
     - 18.796 ns
   * - xproc fixed
     - 1024 B
     - 25.497 ns
   * - xproc varlen
     - 1024 B
     - 31.302 ns
   * - xproc fixed
     - 4096 B
     - 56.762 ns
   * - xproc varlen
     - 4096 B
     - 86.052 ns
