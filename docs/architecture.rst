Architecture
==============

Directory layout (headers)
---------------------------

.. code-block:: text

   include/xproc/
   ├── platform/          # OS / CPU abstractions
   ├── shm/               # Shared memory and layout
   ├── sync/              # atomic_wait, backoff
   ├── ringbuffer/        # fixed / varlen SPSC ring
   ├── ipc/               # channels, observer, runtime
   ├── protocol/          # codecs and helpers
   └── xproc.hpp          # Umbrella include

Ring buffer layer
-----------------

* ``fixed_writer`` / ``fixed_reader``: fixed slot size per channel
* ``varlen_writer`` / ``varlen_reader``: length-prefixed messages
* ``ringbuffer_view``: base pointer / alignment helpers
* ``IRingBuffer``: polymorphic interface (testing)

IPC layer
---------

* Endpoints attach to a shared segment and validate layout
* ``producer_channel`` / ``consumer_channel``: typed send / poll API
* ``ipc_observer``: read-only attach; ``peek`` without consuming
* ``ipc_messaging``: higher-level encoded send/receive helpers
* ``ipc_runtime``: poll loop that copies payloads and dispatches via an executor (see class comments in ``ipc_runtime.hpp``)

Protocol layer
--------------

* ``codec_traits`` and templates for custom codecs
* ``raw_pod_codec<T>`` for trivial POD
* ``bounded_bytes_codec<N>`` for fixed byte arrays
* ``span_codec<MaxN>``: string_view over bytes; decode views ring memory until the handler returns

Shared memory layout
--------------------

#. **Control block**: magic, version, layout type, attachment counters, readiness
#. **Ring metadata**: ``write_pos``, ``read_pos``, ``commit_seq``, ``read_wake_seq`` (see :doc:`design`)
#. **Data region**: message bytes; indices use ``pos % data_capacity``

Synchronization (conceptual)
-----------------------------

* **Lock-free SPSC**: atomics and kernel waits, no mutex in the hot path
* **Two-phase commit**: reserve space, publish with ``commit`` / status fields
* **Waiting**: Linux futex on ``commit_seq`` / ``read_wake_seq``; Windows polling backoff in ``atomic_wait`` on those fields (see :doc:`platforms`)

Performance notes
-----------------

* Prefer fixed-length messages when possible
* Size ``shm_size`` for expected backlog
* Batch work to amortize wake overhead
* View-based codecs require synchronous use inside the poll handler if data must outlive the callback
