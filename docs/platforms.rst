Platform support
================

Linux
-----

* **Shared memory**: ``shm_open`` + ``mmap``
* **Synchronization**: futex (``FUTEX_WAIT`` / ``FUTEX_WAKE``)
* **Requirements**: working POSIX shared memory (``/dev/shm`` or equivalent)

Windows
-------

* **Shared memory**: ``CreateFileMapping`` + ``MapViewOfFile``. The mapped region must be at least ``opts.shm_size``; smaller sections fail ``open``.
* **Synchronization**: ``WaitOnAddress`` / ``WakeByAddress*`` (Windows 8+)
* **Naming**: Logical paths map to ``Local\xproc_<hash>_…`` (FNV-1a + sanitized suffix). Use **unique path strings** (PID, random salt, session id) in tests and services to avoid stale name collisions.
* **``shm::unlink``**: No-op on Windows; do not rely on it to free a name before reuse.

Unsupported platforms
---------------------

CMake fails configuration on non-Linux, non-Windows hosts with an explicit error (see ``CMakeLists.txt``).

Windows tests
-------------

The ``xproc_win32_wait_shm_tests`` target exercises child-process waiting on ``commit_seq`` (see ``tests/win32_wait_shm_test.cpp`` and :doc:`design`).
