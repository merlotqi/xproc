#include <benchmark/benchmark.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <xproc/xproc.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

#if defined(__linux__) || defined(__APPLE__)
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>
#endif

#ifdef XPROC_BENCH_QT
#include <QSharedMemory>
#include <QString>
#endif

#ifdef XPROC_BENCH_POCO
#include <Poco/SharedMemory.h>
#endif

namespace {

struct shm_slot_header {
  std::atomic<std::uint32_t> state;
  std::atomic<std::uint32_t> length;
};

constexpr std::uint32_t k_slot_empty = 0;
constexpr std::uint32_t k_slot_ready = 1;
constexpr std::size_t k_native_data_capacity = 1024 * 1024;
constexpr std::uint32_t k_native_data_align = 8;

inline std::size_t shm_slot_bytes(std::size_t payload_len) { return sizeof(shm_slot_header) + payload_len; }

inline std::size_t align_up(std::size_t size, std::size_t align) { return (size + align - 1) & ~(align - 1); }

std::string unique_segment_name(const char* prefix, std::size_t payload_len, bool leading_slash) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto base =
      std::string("xproc_bench_") + prefix + "_" + std::to_string(payload_len) + "_" + std::to_string(now);
  return leading_slash ? "/" + base : base;
}

template <class Stream>
void run_stream_benchmark(benchmark::State& state, Stream& stream, std::byte fill_byte) {
  const auto payload_len = static_cast<std::size_t>(state.range(0));
  std::vector<std::byte> payload(payload_len, fill_byte);
  std::vector<std::byte> sink(payload_len);

  for (auto _ : state) {
    stream.write(payload.data(), payload_len);
    stream.read(sink.data(), payload_len);
    benchmark::DoNotOptimize(sink.data());
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * payload_len));
}

template <class Mapping>
void run_slot_benchmark(benchmark::State& state, Mapping& mapping) {
  const auto payload_len = static_cast<std::size_t>(state.range(0));
  auto* writer_header = mapping.writer_header();
  auto* writer_payload = mapping.writer_payload();
  auto* reader_header = mapping.reader_header();
  auto* reader_payload = mapping.reader_payload();
  std::vector<std::byte> src(payload_len, std::byte{0x5a});
  std::vector<std::byte> sink(payload_len);

  for (auto _ : state) {
    while (writer_header->state.load(std::memory_order_acquire) != k_slot_empty) {
    }

    std::memcpy(writer_payload, src.data(), payload_len);
    writer_header->length.store(static_cast<std::uint32_t>(payload_len), std::memory_order_relaxed);
    writer_header->state.store(k_slot_ready, std::memory_order_release);

    while (reader_header->state.load(std::memory_order_acquire) != k_slot_ready) {
    }

    const auto len = reader_header->length.load(std::memory_order_acquire);
    std::memcpy(sink.data(), reader_payload, len);
    benchmark::DoNotOptimize(sink.data());
    reader_header->state.store(k_slot_empty, std::memory_order_release);
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * payload_len));
}

struct xproc_slot_mapping {
  explicit xproc_slot_mapping(std::size_t payload_len)
      : name_(unique_segment_name("xproc_shm_slot", payload_len, true)), size_(shm_slot_bytes(payload_len)) {
    xproc::shm::shm::unlink(name_);
    if (!writer_.open(name_, size_, xproc::shm::shm_open_mode::open_create)) {
      throw std::runtime_error("xproc_slot_mapping: failed to create shared memory segment");
    }
    if (!reader_.open(name_, size_, xproc::shm::shm_open_mode::open)) {
      throw std::runtime_error("xproc_slot_mapping: failed to open reader shared memory segment");
    }

    writer_header_ = static_cast<shm_slot_header*>(writer_.addr());
    reader_header_ = static_cast<shm_slot_header*>(reader_.addr());
    if (writer_header_ == nullptr || reader_header_ == nullptr) {
      throw std::runtime_error("xproc_slot_mapping: null mapping address");
    }

    writer_header_->state.store(k_slot_empty, std::memory_order_relaxed);
    writer_header_->length.store(0, std::memory_order_relaxed);
  }

  ~xproc_slot_mapping() { xproc::shm::shm::unlink(name_); }

  shm_slot_header* writer_header() { return writer_header_; }
  std::byte* writer_payload() { return reinterpret_cast<std::byte*>(writer_header_ + 1); }
  shm_slot_header* reader_header() { return reader_header_; }
  std::byte* reader_payload() { return reinterpret_cast<std::byte*>(reader_header_ + 1); }

 private:
  std::string name_;
  std::size_t size_{0};
  xproc::shm::shm writer_;
  xproc::shm::shm reader_;
  shm_slot_header* writer_header_{nullptr};
  shm_slot_header* reader_header_{nullptr};
};

#ifdef _WIN32
struct win32_handle {
  win32_handle() = default;
  explicit win32_handle(HANDLE handle) : handle(handle) {}
  win32_handle(const win32_handle&) = delete;
  win32_handle& operator=(const win32_handle&) = delete;
  win32_handle(win32_handle&& other) noexcept : handle(other.handle) { other.handle = INVALID_HANDLE_VALUE; }
  win32_handle& operator=(win32_handle&& other) noexcept {
    if (this != &other) {
      reset();
      handle = other.handle;
      other.handle = INVALID_HANDLE_VALUE;
    }
    return *this;
  }
  ~win32_handle() { reset(); }

  void reset(HANDLE next = INVALID_HANDLE_VALUE) {
    if (handle != INVALID_HANDLE_VALUE) {
      CloseHandle(handle);
    }
    handle = next;
  }

  HANDLE get() const { return handle; }

 private:
  HANDLE handle{INVALID_HANDLE_VALUE};
};

inline void write_named_pipe_exact(HANDLE pipe, const std::byte* data, std::size_t len) {
  std::size_t offset = 0;
  while (offset < len) {
    DWORD written = 0;
    const auto chunk = static_cast<DWORD>(len - offset);
    if (!WriteFile(pipe, data + offset, chunk, &written, nullptr)) {
      throw std::runtime_error("windows_named_pipe_stream: WriteFile failed");
    }
    offset += static_cast<std::size_t>(written);
  }
}

inline void read_named_pipe_exact(HANDLE pipe, std::byte* data, std::size_t len) {
  std::size_t offset = 0;
  while (offset < len) {
    DWORD read = 0;
    const auto chunk = static_cast<DWORD>(len - offset);
    if (!ReadFile(pipe, data + offset, chunk, &read, nullptr)) {
      throw std::runtime_error("windows_named_pipe_stream: ReadFile failed");
    }
    if (read == 0) {
      throw std::runtime_error("windows_named_pipe_stream: unexpected end of stream");
    }
    offset += static_cast<std::size_t>(read);
  }
}

struct windows_named_pipe_stream {
  explicit windows_named_pipe_stream(std::size_t payload_len)
      : name_("\\\\.\\pipe\\" + unique_segment_name("windows_named_pipe", payload_len, false)) {
    const auto buffer_bytes = static_cast<DWORD>((std::max<std::size_t>)(payload_len * 4, 4096));
    server_.reset(CreateNamedPipeA(name_.c_str(), PIPE_ACCESS_OUTBOUND, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                   1, buffer_bytes, buffer_bytes, 0, nullptr));
    if (server_.get() == INVALID_HANDLE_VALUE) {
      throw std::runtime_error("windows_named_pipe_stream: CreateNamedPipeA failed");
    }

    HANDLE client_handle = INVALID_HANDLE_VALUE;
    std::thread client_connector([&] {
      client_handle = CreateFileA(name_.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
    });

    const BOOL connected = ConnectNamedPipe(server_.get(), nullptr);
    const DWORD connect_error = connected ? ERROR_SUCCESS : GetLastError();
    client_connector.join();

    if (!connected && connect_error != ERROR_PIPE_CONNECTED) {
      throw std::runtime_error("windows_named_pipe_stream: ConnectNamedPipe failed");
    }
    if (client_handle == INVALID_HANDLE_VALUE) {
      throw std::runtime_error("windows_named_pipe_stream: CreateFileA client connect failed");
    }
    client_.reset(client_handle);
  }

  void write(const std::byte* data, std::size_t len) { write_named_pipe_exact(server_.get(), data, len); }

  void read(std::byte* data, std::size_t len) { read_named_pipe_exact(client_.get(), data, len); }

 private:
  std::string name_;
  win32_handle server_;
  win32_handle client_;
};
#endif

#if defined(__linux__) || defined(__APPLE__)
struct unix_fd {
  unix_fd() = default;
  explicit unix_fd(int value) : value(value) {}
  unix_fd(const unix_fd&) = delete;
  unix_fd& operator=(const unix_fd&) = delete;
  unix_fd(unix_fd&& other) noexcept : value(other.value) { other.value = -1; }
  unix_fd& operator=(unix_fd&& other) noexcept {
    if (this != &other) {
      reset();
      value = other.value;
      other.value = -1;
    }
    return *this;
  }
  ~unix_fd() { reset(); }

  void reset(int next = -1) {
    if (value >= 0) {
      close(value);
    }
    value = next;
  }

  int get() const { return value; }

 private:
  int value{-1};
};

inline void write_unix_socket_exact(int fd, const std::byte* data, std::size_t len) {
  std::size_t offset = 0;
  while (offset < len) {
    const auto written = ::write(fd, data + offset, len - offset);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::runtime_error("unix_domain_socket_stream: write failed");
    }
    offset += static_cast<std::size_t>(written);
  }
}

inline void read_unix_socket_exact(int fd, std::byte* data, std::size_t len) {
  std::size_t offset = 0;
  while (offset < len) {
    const auto received = ::read(fd, data + offset, len - offset);
    if (received < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::runtime_error("unix_domain_socket_stream: read failed");
    }
    if (received == 0) {
      throw std::runtime_error("unix_domain_socket_stream: unexpected end of stream");
    }
    offset += static_cast<std::size_t>(received);
  }
}

struct unix_domain_socket_stream {
  explicit unix_domain_socket_stream(std::size_t) {
    int fds[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
      throw std::runtime_error("unix_domain_socket_stream: socketpair failed");
    }
    writer_.reset(fds[0]);
    reader_.reset(fds[1]);
  }

  void write(const std::byte* data, std::size_t len) { write_unix_socket_exact(writer_.get(), data, len); }

  void read(std::byte* data, std::size_t len) { read_unix_socket_exact(reader_.get(), data, len); }

 private:
  unix_fd writer_;
  unix_fd reader_;
};
#endif

#ifdef XPROC_BENCH_QT
struct qt_slot_mapping {
  explicit qt_slot_mapping(std::size_t payload_len)
      : key_(QString::fromStdString(unique_segment_name("qt_shm_slot", payload_len, false))),
        writer_(key_),
        reader_(key_) {
    const auto size = static_cast<int>(shm_slot_bytes(payload_len));
    if (!writer_.create(size)) {
      throw std::runtime_error("qt_slot_mapping: failed to create shared memory segment");
    }
    if (!reader_.attach()) {
      throw std::runtime_error("qt_slot_mapping: failed to attach reader shared memory segment");
    }

    writer_header_ = static_cast<shm_slot_header*>(writer_.data());
    reader_header_ = static_cast<shm_slot_header*>(reader_.data());
    if (writer_header_ == nullptr || reader_header_ == nullptr) {
      throw std::runtime_error("qt_slot_mapping: null mapping address");
    }

    writer_header_->state.store(k_slot_empty, std::memory_order_relaxed);
    writer_header_->length.store(0, std::memory_order_relaxed);
  }

  shm_slot_header* writer_header() { return writer_header_; }
  std::byte* writer_payload() { return reinterpret_cast<std::byte*>(writer_header_ + 1); }
  shm_slot_header* reader_header() { return reader_header_; }
  std::byte* reader_payload() { return reinterpret_cast<std::byte*>(reader_header_ + 1); }

 private:
  QString key_;
  QSharedMemory writer_;
  QSharedMemory reader_;
  shm_slot_header* writer_header_{nullptr};
  shm_slot_header* reader_header_{nullptr};
};
#endif

#ifdef XPROC_BENCH_POCO
struct poco_slot_mapping {
  explicit poco_slot_mapping(std::size_t payload_len)
      : name_(unique_segment_name("poco_shm_slot", payload_len, false)),
        writer_(name_, shm_slot_bytes(payload_len), Poco::SharedMemory::AM_WRITE, nullptr, true),
        reader_(name_, shm_slot_bytes(payload_len), Poco::SharedMemory::AM_WRITE, nullptr, false) {
    writer_header_ = reinterpret_cast<shm_slot_header*>(writer_.begin());
    reader_header_ = reinterpret_cast<shm_slot_header*>(reader_.begin());
    if (writer_header_ == nullptr || reader_header_ == nullptr) {
      throw std::runtime_error("poco_slot_mapping: null mapping address");
    }

    writer_header_->state.store(k_slot_empty, std::memory_order_relaxed);
    writer_header_->length.store(0, std::memory_order_relaxed);
  }

  shm_slot_header* writer_header() { return writer_header_; }
  std::byte* writer_payload() { return reinterpret_cast<std::byte*>(writer_header_ + 1); }
  shm_slot_header* reader_header() { return reader_header_; }
  std::byte* reader_payload() { return reinterpret_cast<std::byte*>(reader_header_ + 1); }

 private:
  std::string name_;
  Poco::SharedMemory writer_;
  Poco::SharedMemory reader_;
  shm_slot_header* writer_header_{nullptr};
  shm_slot_header* reader_header_{nullptr};
};
#endif

void run_native_benchmark(benchmark::State& state, xproc::ipc::channel_type type, std::byte fill_byte) {
  const auto payload_len = static_cast<std::size_t>(state.range(0));
  const auto path = unique_segment_name(type == xproc::ipc::channel_type::fixed ? "native_fixed" : "native_varlen",
                                        payload_len, true);
  xproc::shm::shm::unlink(path);

  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.type = type;
  opts.create_if_missing = true;
  opts.data_align = k_native_data_align;
  if (type == xproc::ipc::channel_type::fixed) {
    opts.item_size = static_cast<std::uint32_t>(payload_len);
    const auto slot_bytes = align_up(payload_len + sizeof(std::uint32_t), k_native_data_align);
    opts.shm_size = xproc::ipc::shm_size_for_data_capacity(slot_bytes);
  } else {
    opts.shm_size = xproc::ipc::shm_size_for_data_capacity(k_native_data_capacity);
  }

  xproc::ipc::producer producer(opts);
  xproc::ipc::consumer consumer(opts);
  std::vector<std::byte> payload(payload_len, fill_byte);
  std::vector<std::byte> sink(payload_len);

  for (auto _ : state) {
    if (type == xproc::ipc::channel_type::fixed) {
      producer.send_fixed_bytes(payload.data(), static_cast<std::uint32_t>(payload_len));
    } else {
      producer.send_varlen(payload.data(), static_cast<std::uint32_t>(payload_len));
    }

    bool got = false;
    while (!got) {
      got = consumer.poll([&](void* data, std::uint32_t len) {
        std::memcpy(sink.data(), data, len);
        benchmark::DoNotOptimize(sink.data());
      });
      if (!got) {
        const auto c = consumer.header()->rb_meta.commit_seq.load(std::memory_order_acquire);
        xproc::sync::atomic_wait<uint32_t>(&consumer.header()->rb_meta.commit_seq, c);
      }
    }
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * payload_len));
  xproc::shm::shm::unlink(path);
}

static void BM_xproc_shm_slot(benchmark::State& state) {
  xproc_slot_mapping mapping(static_cast<std::size_t>(state.range(0)));
  run_slot_benchmark(state, mapping);
}

#ifdef XPROC_BENCH_QT
static void BM_qt_shm_slot(benchmark::State& state) {
  qt_slot_mapping mapping(static_cast<std::size_t>(state.range(0)));
  run_slot_benchmark(state, mapping);
}
#endif

#ifdef XPROC_BENCH_POCO
static void BM_poco_shm_slot(benchmark::State& state) {
  poco_slot_mapping mapping(static_cast<std::size_t>(state.range(0)));
  run_slot_benchmark(state, mapping);
}
#endif

static void BM_xproc_native_fixed(benchmark::State& state) {
  run_native_benchmark(state, xproc::ipc::channel_type::fixed, std::byte{0x5a});
}

static void BM_xproc_native_varlen(benchmark::State& state) {
  run_native_benchmark(state, xproc::ipc::channel_type::varlen, std::byte{0x42});
}

#ifdef _WIN32
static void BM_windows_named_pipe(benchmark::State& state) {
  windows_named_pipe_stream stream(static_cast<std::size_t>(state.range(0)));
  run_stream_benchmark(state, stream, std::byte{0x33});
}
#endif

#if defined(__linux__) || defined(__APPLE__)
static void BM_unix_domain_socket(benchmark::State& state) {
  unix_domain_socket_stream stream(static_cast<std::size_t>(state.range(0)));
  run_stream_benchmark(state, stream, std::byte{0x33});
}
#endif

}  // namespace

BENCHMARK(BM_xproc_shm_slot)->Arg(64)->Arg(1024)->Arg(4096);
#ifdef XPROC_BENCH_QT
BENCHMARK(BM_qt_shm_slot)->Arg(64)->Arg(1024)->Arg(4096);
#endif
#ifdef XPROC_BENCH_POCO
BENCHMARK(BM_poco_shm_slot)->Arg(64)->Arg(1024)->Arg(4096);
#endif
BENCHMARK(BM_xproc_native_fixed)->Arg(64)->Arg(1024)->Arg(4096);
BENCHMARK(BM_xproc_native_varlen)->Arg(64)->Arg(1024)->Arg(4096);
#ifdef _WIN32
BENCHMARK(BM_windows_named_pipe)->Arg(64)->Arg(1024)->Arg(4096);
#endif
#if defined(__linux__) || defined(__APPLE__)
BENCHMARK(BM_unix_domain_socket)->Arg(64)->Arg(1024)->Arg(4096);
#endif
