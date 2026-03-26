#include <atomic>
#include <cassert>
#include <cstring>

#include <xproc/xproc.hpp>

namespace {

using lm = xproc::shm::shm_layout_manager;
using err = xproc::shm::layout_validate_error;

void test_bad_magic() {
  xproc::shm::shm_control_block h{};
  assert(lm::validate_detailed(&h, 100, 0u, 8u) == err::bad_magic);
}

void test_not_ready_times_out() {
  xproc::shm::shm_control_block h{};
  h.magic = lm::EXPECTED_MAGIC;
  assert(lm::validate_detailed(&h, 100, 0u, 8u) == err::not_ready_timeout);
}

void test_version_mismatch() {
  xproc::shm::shm_control_block h{};
  h.magic = lm::EXPECTED_MAGIC;
  h.version_major = lm::VERSION_MAJOR;
  h.version_minor = lm::VERSION_MINOR + 999u;
  h.header_size = sizeof(xproc::shm::shm_control_block);
  h.layout_type = 0;
  h.data_capacity = 4096;
  h.data_alignment = 8;
  h.is_ready.store(true, std::memory_order_release);
  assert(lm::validate_detailed(&h, 100, 0u, 8u) == err::version_mismatch);
}

void test_layout_type_mismatch() {
  xproc::shm::shm_control_block h{};
  h.magic = lm::EXPECTED_MAGIC;
  h.version_major = lm::VERSION_MAJOR;
  h.version_minor = lm::VERSION_MINOR;
  h.header_size = sizeof(xproc::shm::shm_control_block);
  h.layout_type = 0;
  h.data_capacity = 4096;
  h.data_alignment = 8;
  h.is_ready.store(true, std::memory_order_release);
  assert(lm::validate_detailed(&h, 100, 1u, 8u) == err::layout_type_mismatch);
}

void test_ok() {
  xproc::shm::shm_control_block h{};
  h.magic = lm::EXPECTED_MAGIC;
  h.version_major = lm::VERSION_MAJOR;
  h.version_minor = lm::VERSION_MINOR;
  h.header_size = sizeof(xproc::shm::shm_control_block);
  h.layout_type = 0;
  h.data_capacity = 4096;
  h.data_alignment = 8;
  h.is_ready.store(true, std::memory_order_release);
  assert(lm::validate_detailed(&h, 100, 0u, 8u) == err::ok);
  assert(std::strcmp(lm::layout_validate_cstr(err::ok), "ok") == 0);
}

void test_validate_transport_options() {
  xproc::ipc::transport_options bad{};
  bad.path = "";
  bad.shm_size = sizeof(xproc::shm::shm_control_block) + 64;
  bool threw = false;
  try {
    xproc::ipc::validate_transport_options(bad);
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);
}

void test_layout_exception_carries_code() {
  try {
    throw xproc::shm::layout_exception("test: ", err::bad_magic);
  } catch (const xproc::shm::layout_exception &e) {
    assert(e.code() == err::bad_magic);
    const std::error_code ec = e.ec();
    assert(ec == err::bad_magic);
    assert(std::string(ec.category().name()) == "xproc.layout");
  }
}

void test_read_embedded_layout_version() {
  xproc::shm::shm_control_block h{};
  h.version_major = lm::VERSION_MAJOR;
  h.version_minor = lm::VERSION_MINOR;
  const auto v = xproc::shm::read_embedded_layout_version(&h);
  assert(v.major == lm::VERSION_MAJOR);
  assert(v.minor == lm::VERSION_MINOR);
}

void test_default_shm_backend_is_usable_stub() {
  xproc::shm::default_shm_backend b;
  assert(!b.is_attached());
  assert(b.last_os_error() == 0);
}

void test_ringbuffer_facade_and_error_strings() {
  alignas(64) xproc::shm::shm_control_block h{};
  h.data_capacity = 1024;
  h.data_alignment = 8;
  xproc::ringbuffer::control_block_ring_facade view(&h);
  assert(view.capacity_bytes() == 1024);
  assert(view.data_alignment() == 8);
  assert(std::strcmp(xproc::ringbuffer::ringbuffer_error_cstr(xproc::ringbuffer::ringbuffer_error::empty), "empty") ==
         0);
}

}  // namespace

int main() {
  test_bad_magic();
  test_not_ready_times_out();
  test_version_mismatch();
  test_layout_type_mismatch();
  test_ok();
  test_validate_transport_options();
  test_layout_exception_carries_code();
  test_read_embedded_layout_version();
  test_default_shm_backend_is_usable_stub();
  test_ringbuffer_facade_and_error_strings();
  return 0;
}
