Generated Results
-----------------

.. list-table::
   :header-rows: 1
   :widths: 34 26 16

   * - Scenario
     - Metric
     - Value
   * - Thread ping-pong (same process)
     - wait calls/op
     - 1.942
   * - Thread ping-pong (same process)
     - native wait calls/op
     - 2.007e-05
   * - Thread ping-pong (same process)
     - native timeout/op
     - 0
   * - Thread ping-pong (same process)
     - spin iterations/op
     - 0.866
   * - Thread ping-pong (same process)
     - yield iterations/op
     - 2.058e-04
   * - Cross-process ping-pong
     - wait calls/op
     - 1.000
   * - Cross-process ping-pong
     - native wait calls/op
     - 2.895e-05
   * - Cross-process ping-pong
     - native timeout/op
     - 2.895e-05
   * - Cross-process ping-pong
     - spin iterations/op
     - 1.415
   * - Cross-process ping-pong
     - yield iterations/op
     - 2.933e-04
