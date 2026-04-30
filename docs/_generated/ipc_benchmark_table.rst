Generated Results
-----------------

.. note::

   The table below is generated from the most recent local benchmark run.
   Host: Intel(R) Core(TM) i7-14700KF with 64 GB RAM.
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
   * - Poco
     - 64 B
     - 11.000 ns
   * - Qt
     - 64 B
     - 10.700 ns
   * - xproc
     - 64 B
     - 3.770 ns
   * - Poco
     - 1024 B
     - 16.900 ns
   * - Qt
     - 1024 B
     - 17.600 ns
   * - xproc
     - 1024 B
     - 15.000 ns
   * - Poco
     - 4096 B
     - 45.300 ns
   * - Qt
     - 4096 B
     - 42.000 ns
   * - xproc
     - 4096 B
     - 39.600 ns

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
     - 16.700 ns
   * - xproc varlen
     - 64 B
     - 18.400 ns
   * - xproc fixed
     - 1024 B
     - 24.600 ns
   * - xproc varlen
     - 1024 B
     - 31.100 ns
   * - xproc fixed
     - 4096 B
     - 55.800 ns
   * - xproc varlen
     - 4096 B
     - 71.500 ns
