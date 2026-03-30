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
