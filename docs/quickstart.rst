Quick start
===========

Fixed-length channel
--------------------

.. code-block:: cpp

   #include <xproc/xproc.hpp>

   const std::string path = "/my_ipc_channel";
   auto channel = xproc::ipc::make_fixed_channel(path, 256)
       .with_schema_id(1)
       .create(1024 * 1024);

   xproc::ipc::producer producer = channel.open_producer();
   xproc::ipc::consumer consumer = xproc::ipc::attach_fixed_channel(path)
       .with_schema_id(1)
       .open_consumer();

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

   const std::string path = "/my_varlen_channel";
   auto channel = xproc::ipc::make_varlen_channel(path)
       .with_schema_id(2)
       .create(1024 * 1024);

   xproc::ipc::producer producer = channel.open_producer();
   xproc::ipc::consumer consumer = xproc::ipc::attach_varlen_channel(path)
       .with_schema_id(2)
       .open_consumer();

   std::vector<std::byte> data(1024);
   producer.send_varlen(data.data(), static_cast<std::uint32_t>(data.size()));

   consumer.poll([](void* ptr, std::uint32_t len) {
     (void)ptr;
     (void)len;
   });

For advanced flows, ``transport_options`` remains available when you want to set the layout explicitly or override
schema / namespace checks yourself.

Codecs
------

.. code-block:: cpp

   #include <xproc/protocol/codecs.hpp>

   xproc::ipc::send_encoded<xproc::protocol::raw_pod_codec<int>>(producer, 42);
   xproc::ipc::poll_decoded<xproc::protocol::raw_pod_codec<int>>(
       consumer, [](int value) { (void)value; });

See :doc:`advanced` for custom codecs and observer / runtime patterns.
