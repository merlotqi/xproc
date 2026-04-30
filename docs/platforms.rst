Platform support
================

Linux
-----

* **Shared memory**: ``shm_open`` + ``mmap``
* **Synchronization**: futex (``FUTEX_WAIT`` / ``FUTEX_WAKE``)
* **Requirements**: working POSIX shared memory (``/dev/shm`` or equivalent)

Windows
-------

* **Build**: Use an x64 MSVC target (e.g. ``cmake -G "Visual Studio 17 2022" -A x64``). With Ninja + MSVC, open an **x64 Native Tools** command prompt so ``cl`` defines ``_M_X64``; otherwise SDK headers may error with ``C1189: No Target Architecture``.
* **Shared memory**: ``CreateFileMapping`` + ``MapViewOfFile``. The mapped region must be at least ``opts.shm_size``; smaller sections fail ``open``. Within one process, the same logical mapping is backed by a **single** mapped view (reference counted) so producer and consumer share one virtual address range for the segment.
* **Synchronization (vs Linux)**: ``WaitOnAddress`` / ``WakeByAddress*`` pair on **the same virtual address**. Each ``MapViewOfFile`` of a section uses a different VA, and different processes map different VAs, so those primitives **do not** reliably wake waiters across two channel attachments or across processes. xproc therefore implements ``atomic_wait`` on Windows as **spin / yield / sleep polling**; ``atomic_notify_*`` is a **no-op** (waiters observe sequence words via normal loads). Linux uses the futex, which coordinates correctly on shared mappings.
* **Naming**: Logical paths map to ``<namespace>\xproc_<hash>_…`` (FNV-1a + sanitized suffix). Default ``transport_options::win32_object_namespace`` is ``Local``; set ``Global`` only when you need a session‑0 / cross‑session visible object (understand the security implications). Use **unique path strings** (PID, random salt, session id) in tests and services to avoid stale name collisions.
* **``core::unlink``**: No-op on Windows; do not rely on it to free a name before reuse.
* **Socket transport**: TCP backend uses Winsock2; the xproc library links ``ws2_32``. Same synchronization caveats apply only to SHM rings; TCP does not use ``commit_seq`` in shared memory.

Unsupported platforms
---------------------

CMake fails configuration on non-Linux, non-Windows hosts with an explicit error (see ``CMakeLists.txt``).

Windows tests
-------------

The ``xproc_win32_wait_shm_tests`` target exercises child-process waiting on ``commit_seq`` (see ``tests/win32_wait_shm_test.cpp`` and :doc:`design`). For stable runs, use ``ctest -j 1`` (or avoid parallel jobs) so unrelated tests do not compete for named mappings or leave handles locked.

Parallel CTest and SHM names
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``core::unlink`` does not remove kernel names on Windows. Parallel test jobs or stale processes can collide on the same logical ``path``. Prefer **unique** ``transport_options::path`` values and serial ``ctest`` when diagnosing failures.
