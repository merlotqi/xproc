#include <gtest/gtest.h>

#include <atomic>
#include <cstring>
#include <string>
#include <system_error>
#include <xproc/xproc.hpp>

using lm = xproc::shm::layout_manager;
using err = xproc::shm::validate_error;

TEST(LayoutValidate, BadMagic) {
  xproc::shm::control_block h{};
  EXPECT_EQ(lm::validate_detailed(&h, 100, 0u, 8u), err::bad_magic);
}

TEST(LayoutValidate, NotReadyTimesOut) {
  xproc::shm::control_block h{};
  h.magic = lm::expected_magic;
  EXPECT_EQ(lm::validate_detailed(&h, 100, 0u, 8u), err::not_ready_timeout);
}

TEST(LayoutValidate, VersionMismatch) {
  xproc::shm::control_block h{};
  h.magic = lm::expected_magic;
  h.version_major = lm::version_major;
  h.version_minor = lm::version_minor + 999u;
  h.header_size = sizeof(xproc::shm::control_block);
  h.layout_type = 0;
  h.data_capacity = 4096;
  h.data_alignment = 8;
  h.is_ready.store(true, std::memory_order_release);
  EXPECT_EQ(lm::validate_detailed(&h, 100, 0u, 8u), err::version_mismatch);
}

TEST(LayoutValidate, LayoutTypeMismatch) {
  xproc::shm::control_block h{};
  h.magic = lm::expected_magic;
  h.version_major = lm::version_major;
  h.version_minor = lm::version_minor;
  h.header_size = sizeof(xproc::shm::control_block);
  h.layout_type = 0;
  h.data_capacity = 4096;
  h.data_alignment = 8;
  h.is_ready.store(true, std::memory_order_release);
  EXPECT_EQ(lm::validate_detailed(&h, 100, 1u, 8u), err::layout_type_mismatch);
}

TEST(LayoutValidate, Ok) {
  xproc::shm::control_block h{};
  h.magic = lm::expected_magic;
  h.version_major = lm::version_major;
  h.version_minor = lm::version_minor;
  h.header_size = sizeof(xproc::shm::control_block);
  h.layout_type = 0;
  h.data_capacity = 4096;
  h.data_alignment = 8;
  h.is_ready.store(true, std::memory_order_release);
  EXPECT_EQ(lm::validate_detailed(&h, 100, 0u, 8u), err::ok);
  EXPECT_STREQ(lm::validate_cstr(err::ok), "ok");
}

TEST(LayoutValidate, ValidateTransportOptionsRejectsEmptyPath) {
  xproc::ipc::transport_options bad{};
  bad.path = "";
  bad.shm_size = sizeof(xproc::shm::control_block) + 64;
  EXPECT_THROW(xproc::ipc::validate_transport_options(bad), std::invalid_argument);
}

TEST(LayoutValidate, ValidateTransportOptionsRejectsInvalidAlignAndItemSize) {
  xproc::ipc::transport_options bad_align{};
  bad_align.path = "/xproc_bad_align";
  bad_align.shm_size = sizeof(xproc::shm::control_block) + 64;
  bad_align.type = xproc::ipc::channel_type::fixed;
  bad_align.item_size = 4;
  bad_align.data_align = 3;  // not power-of-two >= 4
  EXPECT_THROW(xproc::ipc::validate_transport_options(bad_align), std::invalid_argument);

  xproc::ipc::transport_options bad_item{};
  bad_item.path = "/xproc_bad_item";
  bad_item.shm_size = sizeof(xproc::shm::control_block) + 64;
  bad_item.type = xproc::ipc::channel_type::fixed;
  bad_item.item_size = 0;
  EXPECT_THROW(xproc::ipc::validate_transport_options(bad_item), std::invalid_argument);
}

TEST(LayoutValidate, SharedMemorySizeHelpersAndInferExistingSemantics) {
  constexpr std::size_t data_capacity = 4096;
  constexpr std::size_t shm_size = xproc::ipc::shm_size_for_data_capacity(data_capacity);
  EXPECT_EQ(shm_size, sizeof(xproc::shm::control_block) + data_capacity);
  EXPECT_EQ(xproc::ipc::shm_data_capacity_for_size(shm_size), data_capacity);
  EXPECT_EQ(xproc::ipc::shm_data_capacity_for_size(xproc::ipc::infer_existing_shm_size), 0u);

  xproc::ipc::transport_options creator{};
  creator.path = "/xproc_size_helper_creator";
  creator.shm_size = xproc::ipc::infer_existing_shm_size;
  creator.type = xproc::ipc::channel_type::fixed;
  creator.item_size = 4;
  creator.create_if_missing = true;
  EXPECT_THROW(xproc::ipc::validate_producer_transport_options(creator), std::invalid_argument);
  EXPECT_THROW(xproc::ipc::validate_consumer_transport_options(creator), std::invalid_argument);

  xproc::ipc::transport_options attacher = creator;
  attacher.create_if_missing = false;
  EXPECT_NO_THROW(xproc::ipc::validate_producer_transport_options(attacher));
  EXPECT_NO_THROW(xproc::ipc::validate_consumer_transport_options(attacher));
  EXPECT_NO_THROW(xproc::ipc::validate_observer_transport_options(attacher));
}

TEST(LayoutValidate, LayoutExceptionCarriesCodeAndErrorCode) {
  try {
    throw xproc::shm::layout_exception("test: ", err::bad_magic);
  } catch (const xproc::shm::layout_exception& e) {
    EXPECT_EQ(e.code(), err::bad_magic);
    const std::error_code ec = e.ec();
    EXPECT_EQ(ec, err::bad_magic);
    EXPECT_EQ(std::string(ec.category().name()), "xproc.layout");
  }
}

TEST(LayoutValidate, ReadEmbeddedLayoutVersion) {
  xproc::shm::control_block h{};
  h.version_major = lm::version_major;
  h.version_minor = lm::version_minor;
  const auto v = xproc::shm::read_embedded_version(&h);
  EXPECT_EQ(v.major, lm::version_major);
  EXPECT_EQ(v.minor, lm::version_minor);
}

TEST(LayoutValidate, CreatorMetadataFieldsDefaultToZero) {
  xproc::shm::control_block header{};
  EXPECT_EQ(header.creator_timestamp_ns, 0u);
  EXPECT_EQ(header.creator_flags, 0u);

  xproc::ipc::transport_options opts{};
  EXPECT_EQ(opts.creator_timestamp_ns, 0u);
  EXPECT_EQ(opts.creator_flags, 0u);
}

TEST(LayoutValidate, DefaultShmBackendStub) {
  xproc::shm::default_shm_backend b;
  EXPECT_FALSE(b.is_attached());
  EXPECT_EQ(b.last_os_error(), 0);
}

TEST(LayoutValidate, RingbufferFacadeAndErrorStrings) {
  alignas(64) xproc::shm::control_block h{};
  h.data_capacity = 1024;
  h.data_alignment = 8;
  xproc::ringbuffer::control_block_ring_facade view(&h);
  EXPECT_EQ(view.capacity_bytes(), 1024u);
  EXPECT_EQ(view.data_alignment(), 8u);
  EXPECT_STREQ(xproc::ringbuffer::ringbuffer_error_cstr(xproc::ringbuffer::ringbuffer_error::empty), "empty");
}

TEST(LayoutValidate, FixedItemSizeMismatchOnAttach) {
  const std::string path = "/xproc_layout_fixed_item_size_attach";
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options creator{};
  creator.path = path;
  creator.shm_size = xproc::ipc::shm_size_for_data_capacity(4096);
  creator.type = xproc::ipc::channel_type::fixed;
  creator.item_size = sizeof(std::uint32_t);

  {
    xproc::ipc::producer producer(creator);

    xproc::ipc::transport_options attach = creator;
    attach.shm_size = xproc::ipc::infer_existing_shm_size;
    attach.create_if_missing = false;
    attach.item_size = sizeof(std::uint64_t);

    try {
      (void)xproc::ipc::consumer(attach);
      FAIL() << "expected layout_exception";
    } catch (const xproc::shm::layout_exception& e) {
      EXPECT_EQ(e.code(), err::fixed_item_size_mismatch);
    }
  }

  xproc::shm::shm::unlink(path);
}

TEST(LayoutValidate, AlignmentMismatchOnAttach) {
  const std::string path = "/xproc_layout_alignment_attach";
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options creator{};
  creator.path = path;
  creator.shm_size = xproc::ipc::shm_size_for_data_capacity(4096);
  creator.type = xproc::ipc::channel_type::fixed;
  creator.item_size = sizeof(std::uint32_t);
  creator.data_align = 16u;

  {
    xproc::ipc::producer producer(creator);

    xproc::ipc::transport_options attach = creator;
    attach.shm_size = xproc::ipc::infer_existing_shm_size;
    attach.create_if_missing = false;
    attach.data_align = 8u;

    try {
      (void)xproc::ipc::consumer(attach);
      FAIL() << "expected layout_exception";
    } catch (const xproc::shm::layout_exception& e) {
      EXPECT_EQ(e.code(), err::alignment_invalid);
    }
  }

  xproc::shm::shm::unlink(path);
}

TEST(LayoutValidate, ObserverRejectsLayoutTypeMismatchOnAttach) {
  const std::string path = "/xproc_layout_observer_type_attach";
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options creator{};
  creator.path = path;
  creator.shm_size = xproc::ipc::shm_size_for_data_capacity(4096);
  creator.type = xproc::ipc::channel_type::fixed;
  creator.item_size = sizeof(std::uint32_t);

  {
    xproc::ipc::producer producer(creator);

    xproc::ipc::transport_options observer_opts = creator;
    observer_opts.shm_size = xproc::ipc::infer_existing_shm_size;
    observer_opts.create_if_missing = false;
    observer_opts.type = xproc::ipc::channel_type::varlen;

    try {
      (void)xproc::ipc::observer(observer_opts);
      FAIL() << "expected layout_exception";
    } catch (const xproc::shm::layout_exception& e) {
      EXPECT_EQ(e.code(), err::layout_type_mismatch);
    }
  }

  xproc::shm::shm::unlink(path);
}

TEST(LayoutValidate, ObserverRejectsSchemaMismatchOnAttach) {
  const std::string path = "/xproc_layout_observer_schema_attach";
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options creator{};
  creator.path = path;
  creator.shm_size = xproc::ipc::shm_size_for_data_capacity(4096);
  creator.type = xproc::ipc::channel_type::varlen;
  creator.schema_id = 0x01020304u;

  {
    xproc::ipc::producer producer(creator);

    xproc::ipc::transport_options observer_opts = creator;
    observer_opts.shm_size = xproc::ipc::infer_existing_shm_size;
    observer_opts.create_if_missing = false;
    observer_opts.schema_id += 1u;

    try {
      (void)xproc::ipc::observer(observer_opts);
      FAIL() << "expected layout_exception";
    } catch (const xproc::shm::layout_exception& e) {
      EXPECT_EQ(e.code(), err::schema_id_mismatch);
    }
  }

  xproc::shm::shm::unlink(path);
}

TEST(LayoutValidate, AttachIgnoresCreatorMetadataMismatch) {
  const std::string path = "/xproc_layout_creator_metadata_attach";
  xproc::shm::shm::unlink(path);

  const auto created = xproc::ipc::make_fixed_channel(path, sizeof(std::uint32_t))
                           .with_creator_timestamp_ns(123456789u)
                           .with_creator_flags(0x55AAu)
                           .create(4096);

  xproc::ipc::producer producer(created.options());

  xproc::ipc::transport_options attach = created.options();
  attach.shm_size = xproc::ipc::infer_existing_shm_size;
  attach.create_if_missing = false;
  attach.creator_timestamp_ns += 1u;
  attach.creator_flags += 1u;

  EXPECT_NO_THROW({
    xproc::ipc::consumer consumer(attach);
    (void)consumer;
  });

  xproc::shm::shm::unlink(path);
}

TEST(LayoutValidate, VersionMismatchOnAttach) {
  const std::string path = "/xproc_layout_version_attach";
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options creator{};
  creator.path = path;
  creator.shm_size = xproc::ipc::shm_size_for_data_capacity(4096);
  creator.type = xproc::ipc::channel_type::fixed;
  creator.item_size = sizeof(std::uint32_t);

  {
    xproc::ipc::producer producer(creator);
    producer.header()->version_minor = lm::version_minor + 1u;

    xproc::ipc::transport_options attach = creator;
    attach.shm_size = xproc::ipc::infer_existing_shm_size;
    attach.create_if_missing = false;

    try {
      (void)xproc::ipc::consumer(attach);
      FAIL() << "expected layout_exception";
    } catch (const xproc::shm::layout_exception& e) {
      EXPECT_EQ(e.code(), err::version_mismatch);
    }
  }

  xproc::shm::shm::unlink(path);
}

TEST(LayoutValidate, ObserverRejectsVersionMismatchOnAttach) {
  const std::string path = "/xproc_layout_observer_version_attach";
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options creator{};
  creator.path = path;
  creator.shm_size = xproc::ipc::shm_size_for_data_capacity(4096);
  creator.type = xproc::ipc::channel_type::varlen;
  creator.schema_id = 0x99u;

  {
    xproc::ipc::producer producer(creator);
    producer.header()->version_minor = lm::version_minor + 1u;

    xproc::ipc::transport_options observer_opts = creator;
    observer_opts.shm_size = xproc::ipc::infer_existing_shm_size;
    observer_opts.create_if_missing = false;

    try {
      (void)xproc::ipc::observer(observer_opts);
      FAIL() << "expected layout_exception";
    } catch (const xproc::shm::layout_exception& e) {
      EXPECT_EQ(e.code(), err::version_mismatch);
    }
  }

  xproc::shm::shm::unlink(path);
}
