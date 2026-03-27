Design notes
============

This document expands on behavior referenced from the codebase and CMake messages.

Layout validation and ``is_ready``
-----------------------------------

The creator initializes the control block and sets ``is_ready`` when the segment is safe for attachers. Non-creators spin briefly waiting for ``is_ready`` during validation (see ``shm_layout_manager::validate_detailed`` and ``is_ready_spin_limit_v`` in ``shm_layout_manager.hpp``).

Monotonic positions and the data ring
---------------------------------------

``write_pos`` and ``read_pos`` are monotonic logical byte offsets into the stream. The physical index is ``pos % data_capacity``. Producers and consumers must respect SPSC rules so that each message’s logical span maps to valid ring usage; the fixed-slot implementation assumes each reserved slot fits in the ring’s contiguous mapping rules for that API (see tests and benchmarks for in-process setups).

Windows naming and lifetime
-----------------------------

Paths are hashed and prefixed under ``Local\`` for kernel object names. Because ``shm::unlink`` is a no-op on Windows, processes must choose **new unique names** when a previous mapping may still be referenced.

Child process tests (Windows)
-------------------------------

``win32_wait_shm_test`` spawns a child that blocks on ``commit_seq`` and receives a message from the parent, exercising the wait/wake path across processes.

Further reading
---------------

* Repository ``README.md`` for end-user feature list and examples
* Header comments on ``ipc_runtime``, ``transport_options``, and layout enums
