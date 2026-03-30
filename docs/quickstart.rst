Quick start
===========

Fixed-length channel
--------------------

.. code-block:: cpp

   #include <xproc/xproc.hpp>

   xproc::ipc::transport_options opts;
   opts.path = "/my_ipc_channel";
   opts.shm_size = 1024 * 1024;
   opts.type = xproc::ipc::channel_type::fixed;
   opts.create_if_missing = true;
   opts.item_size = 256;

   xproc::ipc::producer producer(opts);
   xproc::ipc::consumer consumer(opts);

   std::string message = "Hello, IPC!";
   producer.send_fixed_bytes(
       reinterpret_cast<const std::byte*>(message.data()),
       static_cast<std::uint32_t>(message.size()));

   consumer.poll([](void* data, std::uint32_t len) {
     std::string received(static_cast<const char*>(data),
                          static_cast<std::size_t>(len));
     (void)received;
   });

Variable-length channel
-----------------------

.. code-block:: cpp

   opts.type = xproc::ipc::channel_type::varlen;

   xproc::ipc::producer producer(opts);
   xproc::ipc::consumer consumer(opts);

   std::vector<std::byte> data(1024);
   producer.send_varlen(data.data(), static_cast<std::uint32_t>(data.size()));

   consumer.poll([](void* ptr, std::uint32_t len) {
     (void)ptr;
     (void)len;
   });

Codecs
------

.. code-block:: cpp

   #include <xproc/protocol/codecs.hpp>

   xproc::ipc::send_encoded<xproc::protocol::raw_pod_codec<int>>(producer, 42);
   xproc::ipc::poll_decoded<xproc::protocol::raw_pod_codec<int>>(
       consumer, [](int value) { (void)value; });

See :doc:`advanced` for custom codecs and observer / runtime patterns.
