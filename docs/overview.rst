Overview
========

Features
--------

* Lock-free SPSC communication with low latency
* Linux and Windows only (other platforms are rejected at CMake configure time)
* Fixed-length and variable-length channel modes
* Variable-length payloads can avoid extra copies; pointers are valid only for the duration of ``poll`` / ``peek`` callbacks
* Read-only observer attach (``ipc_observer``) for snapshots and ``peek`` without advancing ``read_pos`` (weak consistency if a consumer runs concurrently)
* Built-in codecs; optional JSON (nlohmann/json) and Protocol Buffers behind CMake options
* Cache-line–aligned control block to reduce false sharing

Error surfaces
--------------

* ``std::invalid_argument`` / ``std::logic_error`` for API misuse
* ``xproc::shm::layout_exception`` with ``layout_validate_error code()`` for shared-memory layout failures
* ``xproc::ipc::codec_exception`` with ``codec_error code()`` for encode/decode failures
* ``shm::last_os_error()`` after a failed ``shm::open()``

Main entry point
----------------

Include the umbrella header:

.. code-block:: cpp

   #include <xproc/xproc.hpp>

The public layout under ``include/xproc/`` is summarized in :doc:`architecture`.
