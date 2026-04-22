Design notes
============

This document expands on behavior referenced from the codebase and CMake messages.

Layout validation and ``is_ready``
-----------------------------------

The endpoint that actually creates the segment initializes the control block and sets ``is_ready`` when the segment is safe for attachers. Non-creators spin briefly waiting for ``is_ready`` during validation (see ``layout_manager::validate_detailed`` and ``is_ready_spin_limit_v`` in ``layout_manager.hpp``).

Monotonic positions and the data ring
---------------------------------------

``write_pos`` and ``read_pos`` are monotonic logical byte offsets into the stream. The physical index is ``pos % data_capacity``. Producers and consumers must respect SPSC rules so that each message’s logical span maps to valid ring usage; the fixed-slot implementation assumes each reserved slot fits in the ring’s contiguous mapping rules for that API (see tests and benchmarks for in-process setups).

Windows naming and lifetime
-----------------------------

Paths are hashed and prefixed under ``<namespace>\`` (default ``Local\``; optional ``Global\`` via ``transport_options::win32_object_namespace``) for kernel object names. Because ``shm::unlink`` is a no-op on Windows, processes must choose **new unique names** when a previous mapping may still be referenced.

Windows vs Linux synchronization semantics
-------------------------------------------

On Linux, futex wait/wake is tied to the **shared memory page** backing the atomic, so producer and consumer (or two processes) coordinate correctly even when each has its own virtual address for the mapping.

On Windows, ``WaitOnAddress`` / ``WakeByAddress*`` coordinate on a **specific virtual address** in a process. Separate ``MapViewOfFile`` calls (and mappings in different processes) use **different** virtual addresses for the same file offset, so address-based wake does **not** pair across typical producer/consumer attachments. xproc therefore uses **polling with backoff** in ``atomic_wait`` on Windows; ``atomic_notify_*`` is a no-op. Correctness follows from coherent loads of ``commit_seq`` and ``read_wake_seq``.

Within a **single** process, the Win32 ``shm`` implementation reference-counts **one** mapped view per ``(object name, size, map access)`` so producer and consumer channels share the **same** virtual address range for a segment (reducing mappings and matching Linux-style in-process behavior for those atomics).

Ring buffer capacity (SPSC)
----------------------------

With very small ``data_capacity`` relative to the maximum in-flight message size, the producer may block in ``reserve`` waiting for space while the consumer blocks in ``atomic_wait`` on ``commit_seq``. On Windows both sides eventually observe updates via polling, but **extremely** tight rings stress the implementation and tests; prefer a data region large enough for several worst-case messages (see in-process ring buffer tests).

Child process tests (Windows)
-------------------------------

``win32_wait_shm_test`` spawns a child that waits on ``commit_seq`` and receives a message from the parent, exercising shared memory and polling wait behavior across processes.

Transport backends (SHM vs TCP)
--------------------------------

``transport_options::backend`` selects how bytes move between producer and consumer:

* **``shm`` (default)**: same-machine shared memory + in-band ring atomics (futex on Linux, polling wait on Windows). ``path`` and ``shm_size`` are required; this is the original xproc model.
* **``socket``**: TCP framing over loopback or a network with IPv4 / IPv6 connect support. The consumer binds with ``socket_listen=true`` and an optional ephemeral ``socket_port`` (``0`` chooses a free port); the producer connects with ``socket_listen=false`` to ``socket_host`` / ``socket_port``. Listen prefers an IPv6 socket with ``IPV6_V6ONLY=0`` so one listener can accept both IPv6 and IPv4-mapped peers when the platform allows it, and falls back to IPv4 otherwise. Fixed channels send ``item_size`` bytes per message on the wire; variable channels send a little-endian ``uint32_t`` length plus payload. This path does **not** expose a real ``control_block``; ``ipc_observer``-style attach counts are SHM-only. ``IProducerChannel`` / ``IConsumerChannel`` abstract the send/poll surface; ``create_producer_transport`` / ``create_consumer_transport`` build SHM or socket implementations. RDMA or other NIC offload would be additional backends with their own wire protocol, not a drop-in replacement for the ring’s atomics.

Further reading
---------------

* Repository ``README.md`` for end-user feature list and examples
* Header comments on ``ipc_runtime``, ``transport_options``, and layout enums
