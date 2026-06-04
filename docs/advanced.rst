Advanced topics
===============

Observer (read-only)
--------------------

.. code-block:: cpp

   xproc::ipc::ipc_observer observer(opts);
   observer.peek([](const void* data, std::uint32_t len) {
     (void)data;
     (void)len;
   });

``peek`` does not advance ``read_pos``; consistency is best-effort if a consumer is active.

``ipc_runtime``
---------------

``ipc_runtime`` polls the consumer channel, **copies** each message into ``std::vector<std::uint8_t>``, then invokes ``pool_executor(task)``. The task should eventually call your handler with the copied bytes. See ``include/xproc/ipc/ipc_runtime.hpp`` for the executor contract, ``stop()``, and exception behavior.

.. code-block:: cpp

   xproc::ipc::consumer consumer(opts);
   xproc::ipc::ipc_runtime rt(consumer);

   auto pool = [](auto task) { task(); };  // replace with real thread pool

   rt.run(pool, [](const std::uint8_t* data, std::size_t len) {
     (void)data;
     (void)len;
   });
   rt.stop();

Error handling (summary)
-------------------------

* ``validate_transport_options``: path, ``shm_size``, ``item_size`` (fixed), ``data_align``; on Windows, ``win32_object_namespace`` must be ``Local`` or ``Global``
* Layout failures: ``layout_exception`` + ``validate_error``
* Role misuse on channels: ``std::logic_error``
* Failed SHM open: ``last_os_error()``

Thread safety
-------------

* **SPSC**: one producer and one consumer per logical channel
* Atomics use acquire/release where required for publication and consumption
* Do not share a channel instance across threads without external synchronization

Contributing (tests)
--------------------

Use **unique** ``transport_options::path`` values in tests (PID, random suffix). On Windows, ``unlink`` does not remove mapping names; uniqueness avoids collisions across runs.
