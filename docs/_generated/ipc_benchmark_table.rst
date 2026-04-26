Generated Results
-----------------

.. note::

   The table below is generated from the most recent local benchmark run.
   Missing frameworks mean the corresponding optional dependency was not
   enabled or not available when the benchmark executable was built.

Fair Shared-Memory Baseline
^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 24 14 18

   * - Framework
     - Payload
     - Latency
   * - Qt
     - 64 B
     - 39.059 ns
   * - xproc
     - 64 B
     - 41.240 ns
   * - Qt
     - 1024 B
     - 46.500 ns
   * - xproc
     - 1024 B
     - 47.228 ns
   * - Qt
     - 4096 B
     - 73.960 ns
   * - xproc
     - 4096 B
     - 73.547 ns

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
     - 360.066 ns
   * - xproc varlen
     - 64 B
     - 348.556 ns
   * - xproc fixed
     - 1024 B
     - 378.456 ns
   * - xproc varlen
     - 1024 B
     - 381.735 ns
   * - xproc fixed
     - 4096 B
     - 424.902 ns
   * - xproc varlen
     - 4096 B
     - 436.472 ns
