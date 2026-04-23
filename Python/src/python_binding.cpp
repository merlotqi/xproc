#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "xproc_c.h"

namespace py = pybind11;

namespace {

class status_error : public std::runtime_error {
 public:
  status_error(xproc_c_status status, xproc_c_layout_error layout_error, std::string message)
      : std::runtime_error(std::move(message)), status(status), layout_error(layout_error) {}

  xproc_c_status status;
  xproc_c_layout_error layout_error;
};

struct transport_options {
  transport_options() {
    xproc_c_options defaults{};
    xproc_c_options_init(&defaults);
    backend = defaults.backend;
    shm_size = defaults.shm_size;
    item_size = defaults.item_size;
    data_align = defaults.data_align;
    schema_id = defaults.schema_id;
    create_if_missing = (defaults.create_if_missing != 0);
    channel_type = defaults.channel_type;
    if (defaults.win32_object_namespace != nullptr) {
      win32_object_namespace = defaults.win32_object_namespace;
    }
    if (defaults.socket_host != nullptr) {
      socket_host = defaults.socket_host;
    }
    socket_port = defaults.socket_port;
    socket_listen = (defaults.socket_listen != 0);
    socket_connect_retries = defaults.socket_connect_retries;
    socket_connect_retry_ms = defaults.socket_connect_retry_ms;
  }

  xproc_c_backend backend{};
  std::optional<std::string> path;
  std::size_t shm_size{};
  std::uint32_t item_size{};
  std::uint32_t data_align{};
  std::uint64_t schema_id{};
  bool create_if_missing{};
  xproc_c_channel_type channel_type{};
  std::optional<std::string> win32_object_namespace;
  std::optional<std::string> socket_host;
  std::uint16_t socket_port{};
  bool socket_listen{};
  int socket_connect_retries{};
  int socket_connect_retry_ms{};
};

struct snapshot {
  std::uint64_t write_pos{};
  std::uint64_t read_pos{};
  std::uint32_t commit_seq{};
  std::uint32_t read_wake_seq{};
  std::uint32_t attach_count{};
  std::int32_t producer_pid{};
};

struct marshalled_options {
  explicit marshalled_options(const transport_options& options) {
    xproc_c_options_init(&value);
    value.backend = options.backend;
    value.shm_size = options.shm_size;
    value.item_size = options.item_size;
    value.data_align = options.data_align;
    value.schema_id = options.schema_id;
    value.create_if_missing = options.create_if_missing ? 1 : 0;
    value.channel_type = options.channel_type;
    value.socket_port = options.socket_port;
    value.socket_listen = options.socket_listen ? 1 : 0;
    value.socket_connect_retries = options.socket_connect_retries;
    value.socket_connect_retry_ms = options.socket_connect_retry_ms;

    if (options.path.has_value()) {
      path = *options.path;
      value.path = path.c_str();
    } else {
      value.path = nullptr;
    }

    if (options.win32_object_namespace.has_value()) {
      win32_object_namespace = *options.win32_object_namespace;
      value.win32_object_namespace = win32_object_namespace.c_str();
    } else {
      value.win32_object_namespace = nullptr;
    }

    if (options.socket_host.has_value()) {
      socket_host = *options.socket_host;
      value.socket_host = socket_host.c_str();
    } else {
      value.socket_host = nullptr;
    }
  }

  xproc_c_options value{};
  std::string path;
  std::string win32_object_namespace;
  std::string socket_host;
};

transport_options make_transport_options(const xproc_c_options& options) {
  transport_options out;
  out.backend = options.backend;
  if (options.path != nullptr) {
    out.path = std::string(options.path);
  } else {
    out.path.reset();
  }
  out.shm_size = options.shm_size;
  out.item_size = options.item_size;
  out.data_align = options.data_align;
  out.schema_id = options.schema_id;
  out.create_if_missing = (options.create_if_missing != 0);
  out.channel_type = options.channel_type;
  if (options.win32_object_namespace != nullptr) {
    out.win32_object_namespace = std::string(options.win32_object_namespace);
  } else {
    out.win32_object_namespace.reset();
  }
  if (options.socket_host != nullptr) {
    out.socket_host = std::string(options.socket_host);
  } else {
    out.socket_host.reset();
  }
  out.socket_port = options.socket_port;
  out.socket_listen = (options.socket_listen != 0);
  out.socket_connect_retries = options.socket_connect_retries;
  out.socket_connect_retry_ms = options.socket_connect_retry_ms;
  return out;
}

[[noreturn]] void throw_status(const char* context, xproc_c_status status) {
  const xproc_c_layout_error layout_error = xproc_c_last_layout_error();
  const char* raw = xproc_c_last_error_message();
  std::string message = context;
  message += ": ";
  if (raw != nullptr && raw[0] != '\0') {
    message += raw;
  } else {
    message += xproc_c_status_string(status);
  }
  if (layout_error != XPROC_C_LAYOUT_ERROR_NONE) {
    message += " (layout_error=";
    message += xproc_c_layout_error_string(layout_error);
    message += ")";
  }
  throw status_error(status, layout_error, std::move(message));
}

void require_status_ok(const char* context, xproc_c_status status) {
  if (status != XPROC_C_STATUS_OK) {
    throw_status(context, status);
  }
}

struct message_bytes {
  const void* data{nullptr};
  std::uint32_t len{0};
  std::string owned;
  py::object keepalive;
};

std::uint32_t narrow_size(std::size_t value, const char* context) {
  if (value > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
    throw py::value_error(std::string(context) + ": payload exceeds uint32 message limit");
  }
  return static_cast<std::uint32_t>(value);
}

message_bytes coerce_message(py::handle value, const char* context) {
  message_bytes out;
  if (py::isinstance<py::str>(value) || py::isinstance<py::bytes>(value)) {
    out.owned = py::cast<std::string>(value);
    out.data = out.owned.data();
    out.len = narrow_size(out.owned.size(), context);
    return out;
  }

  try {
    py::buffer buffer = py::reinterpret_borrow<py::buffer>(value);
    py::buffer_info info = buffer.request();
    if (info.ndim < 1) {
      throw py::type_error(std::string(context) + ": expected a bytes-like object");
    }
    out.keepalive = py::reinterpret_borrow<py::object>(value);
    out.data = info.ptr;
    out.len = narrow_size(static_cast<std::size_t>(info.size) * static_cast<std::size_t>(info.itemsize), context);
    return out;
  } catch (const py::cast_error&) {
  } catch (const py::type_error&) {
  } catch (const py::error_already_set&) {
  }

  throw py::type_error(std::string(context) + ": expected bytes, bytearray, memoryview, buffer, or str");
}

py::object poll_copy_impl(xproc_c_consumer* handle, const char* context) {
  std::uint32_t required_len = 0;
  const xproc_c_status probe = xproc_c_consumer_poll_copy(handle, nullptr, 0, &required_len);
  if (probe == XPROC_C_STATUS_AGAIN) {
    return py::none();
  }
  if (probe == XPROC_C_STATUS_OK) {
    return py::bytes("", 0);
  }
  if (probe != XPROC_C_STATUS_BUFFER_TOO_SMALL) {
    throw_status(context, probe);
  }

  std::string payload(required_len, '\0');
  const xproc_c_status status = xproc_c_consumer_poll_copy(handle, payload.data(), required_len, &required_len);
  require_status_ok(context, status);
  payload.resize(required_len);
  return py::bytes(payload);
}

py::object peek_copy_impl(xproc_c_observer* handle, const char* context) {
  std::uint32_t required_len = 0;
  const xproc_c_status probe = xproc_c_observer_peek_copy(handle, nullptr, 0, &required_len);
  if (probe == XPROC_C_STATUS_AGAIN) {
    return py::none();
  }
  if (probe == XPROC_C_STATUS_OK) {
    return py::bytes("", 0);
  }
  if (probe != XPROC_C_STATUS_BUFFER_TOO_SMALL) {
    throw_status(context, probe);
  }

  std::string payload(required_len, '\0');
  const xproc_c_status status = xproc_c_observer_peek_copy(handle, payload.data(), required_len, &required_len);
  require_status_ok(context, status);
  payload.resize(required_len);
  return py::bytes(payload);
}

const char* backend_name(xproc_c_backend backend) {
  switch (backend) {
    case XPROC_C_BACKEND_SHARED_MEMORY:
      return "Backend.SHARED_MEMORY";
    case XPROC_C_BACKEND_SOCKET:
      return "Backend.SOCKET";
  }
  return "Backend.UNKNOWN";
}

const char* channel_type_name(xproc_c_channel_type type) {
  switch (type) {
    case XPROC_C_CHANNEL_FIXED:
      return "ChannelType.FIXED";
    case XPROC_C_CHANNEL_VARLEN:
      return "ChannelType.VARLEN";
  }
  return "ChannelType.UNKNOWN";
}

class producer {
 public:
  explicit producer(const transport_options& options) {
    marshalled_options marshalled(options);
    require_status_ok("Producer.open", xproc_c_producer_open(&marshalled.value, &handle_));
  }

  ~producer() { close(); }

  producer(const producer&) = delete;
  producer& operator=(const producer&) = delete;
  producer(producer&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
  producer& operator=(producer&& other) noexcept {
    if (this != &other) {
      close();
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }

  void close() {
    if (handle_ != nullptr) {
      xproc_c_producer_close(handle_);
      handle_ = nullptr;
    }
  }

  transport_options options() const {
    require_open("Producer.options");
    xproc_c_options out{};
    require_status_ok("Producer.options", xproc_c_producer_options(handle_, &out));
    return make_transport_options(out);
  }

  void send_fixed_bytes(py::handle value) {
    require_open("Producer.send_fixed_bytes");
    const message_bytes bytes = coerce_message(value, "Producer.send_fixed_bytes");
    require_status_ok("Producer.send_fixed_bytes", xproc_c_producer_send_fixed_bytes(handle_, bytes.data, bytes.len));
  }

  void send_fixed_sized(py::handle value) {
    require_open("Producer.send_fixed_sized");
    const message_bytes bytes = coerce_message(value, "Producer.send_fixed_sized");
    require_status_ok("Producer.send_fixed_sized", xproc_c_producer_send_fixed_sized(handle_, bytes.data, bytes.len));
  }

  void send_varlen(py::handle value) {
    require_open("Producer.send_varlen");
    const message_bytes bytes = coerce_message(value, "Producer.send_varlen");
    require_status_ok("Producer.send_varlen", xproc_c_producer_send_varlen(handle_, bytes.data, bytes.len));
  }

  std::uint16_t socket_port() const {
    require_open("Producer.socket_port");
    std::uint16_t out = 0;
    require_status_ok("Producer.socket_port", xproc_c_producer_socket_port(handle_, &out));
    return out;
  }

 private:
  void require_open(const char* context) const {
    if (handle_ == nullptr) {
      throw std::runtime_error(std::string(context) + ": handle is closed");
    }
  }

  xproc_c_producer* handle_{nullptr};
};

class consumer {
 public:
  explicit consumer(const transport_options& options) {
    marshalled_options marshalled(options);
    require_status_ok("Consumer.open", xproc_c_consumer_open(&marshalled.value, &handle_));
  }

  ~consumer() { close(); }

  consumer(const consumer&) = delete;
  consumer& operator=(const consumer&) = delete;
  consumer(consumer&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
  consumer& operator=(consumer&& other) noexcept {
    if (this != &other) {
      close();
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }

  void close() {
    if (handle_ != nullptr) {
      xproc_c_consumer_close(handle_);
      handle_ = nullptr;
    }
  }

  transport_options options() const {
    require_open("Consumer.options");
    xproc_c_options out{};
    require_status_ok("Consumer.options", xproc_c_consumer_options(handle_, &out));
    return make_transport_options(out);
  }

  std::uint32_t pending_len() const {
    require_open("Consumer.pending_len");
    std::uint32_t out = 0;
    require_status_ok("Consumer.pending_len", xproc_c_consumer_pending_len(handle_, &out));
    return out;
  }

  py::object poll_copy() {
    require_open("Consumer.poll_copy");
    return poll_copy_impl(handle_, "Consumer.poll_copy");
  }

  void wait() {
    require_open("Consumer.wait");
    py::gil_scoped_release release;
    require_status_ok("Consumer.wait", xproc_c_consumer_wait(handle_));
  }

  std::uint16_t socket_port() const {
    require_open("Consumer.socket_port");
    std::uint16_t out = 0;
    require_status_ok("Consumer.socket_port", xproc_c_consumer_socket_port(handle_, &out));
    return out;
  }

 private:
  void require_open(const char* context) const {
    if (handle_ == nullptr) {
      throw std::runtime_error(std::string(context) + ": handle is closed");
    }
  }

  xproc_c_consumer* handle_{nullptr};
};

class observer {
 public:
  explicit observer(const transport_options& options) {
    marshalled_options marshalled(options);
    require_status_ok("Observer.open", xproc_c_observer_open(&marshalled.value, &handle_));
  }

  ~observer() { close(); }

  observer(const observer&) = delete;
  observer& operator=(const observer&) = delete;
  observer(observer&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
  observer& operator=(observer&& other) noexcept {
    if (this != &other) {
      close();
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }

  void close() {
    if (handle_ != nullptr) {
      xproc_c_observer_close(handle_);
      handle_ = nullptr;
    }
  }

  transport_options options() const {
    require_open("Observer.options");
    xproc_c_options out{};
    require_status_ok("Observer.options", xproc_c_observer_options(handle_, &out));
    return make_transport_options(out);
  }

  snapshot get_snapshot() const {
    require_open("Observer.snapshot");
    xproc_c_snapshot raw{};
    require_status_ok("Observer.snapshot", xproc_c_observer_snapshot(handle_, &raw));
    snapshot out;
    out.write_pos = raw.write_pos;
    out.read_pos = raw.read_pos;
    out.commit_seq = raw.commit_seq;
    out.read_wake_seq = raw.read_wake_seq;
    out.attach_count = raw.attach_count;
    out.producer_pid = raw.producer_pid;
    return out;
  }

  py::object peek_copy() {
    require_open("Observer.peek_copy");
    return peek_copy_impl(handle_, "Observer.peek_copy");
  }

 private:
  void require_open(const char* context) const {
    if (handle_ == nullptr) {
      throw std::runtime_error(std::string(context) + ": handle is closed");
    }
  }

  xproc_c_observer* handle_{nullptr};
};

std::string repr_transport_options(const transport_options& options) {
  std::string repr = "TransportOptions(";
  repr += "backend=" + std::string(backend_name(options.backend));
  repr += ", path=" + (options.path.has_value() ? ("'" + *options.path + "'") : "None");
  repr += ", shm_size=" + std::to_string(options.shm_size);
  repr += ", item_size=" + std::to_string(options.item_size);
  repr += ", data_align=" + std::to_string(options.data_align);
  repr += ", schema_id=" + std::to_string(options.schema_id);
  repr += ", create_if_missing=" + std::string(options.create_if_missing ? "True" : "False");
  repr += ", channel_type=" + std::string(channel_type_name(options.channel_type));
  repr += ")";
  return repr;
}

std::string repr_snapshot(const snapshot& value) {
  return "Snapshot(write_pos=" + std::to_string(value.write_pos) + ", read_pos=" + std::to_string(value.read_pos) +
         ", commit_seq=" + std::to_string(value.commit_seq) + ", read_wake_seq=" + std::to_string(value.read_wake_seq) +
         ", attach_count=" + std::to_string(value.attach_count) +
         ", producer_pid=" + std::to_string(value.producer_pid) + ")";
}

}  // namespace

PYBIND11_MODULE(_xproc_pybind, m) {
  py::register_exception<status_error>(m, "XprocError");

  py::enum_<xproc_c_status>(m, "Status")
      .value("OK", XPROC_C_STATUS_OK)
      .value("AGAIN", XPROC_C_STATUS_AGAIN)
      .value("BUFFER_TOO_SMALL", XPROC_C_STATUS_BUFFER_TOO_SMALL)
      .value("INVALID_ARGUMENT", XPROC_C_STATUS_INVALID_ARGUMENT)
      .value("LOGIC_ERROR", XPROC_C_STATUS_LOGIC_ERROR)
      .value("LAYOUT_ERROR", XPROC_C_STATUS_LAYOUT_ERROR)
      .value("RUNTIME_ERROR", XPROC_C_STATUS_RUNTIME_ERROR)
      .value("NO_MEMORY", XPROC_C_STATUS_NO_MEMORY)
      .value("INTERNAL_ERROR", XPROC_C_STATUS_INTERNAL_ERROR)
      .export_values();

  py::enum_<xproc_c_endpoint_kind>(m, "EndpointKind")
      .value("PRODUCER", XPROC_C_ENDPOINT_PRODUCER)
      .value("CONSUMER", XPROC_C_ENDPOINT_CONSUMER)
      .value("OBSERVER", XPROC_C_ENDPOINT_OBSERVER)
      .export_values();

  py::enum_<xproc_c_backend>(m, "Backend")
      .value("SHARED_MEMORY", XPROC_C_BACKEND_SHARED_MEMORY)
      .value("SOCKET", XPROC_C_BACKEND_SOCKET)
      .export_values();

  py::enum_<xproc_c_channel_type>(m, "ChannelType")
      .value("FIXED", XPROC_C_CHANNEL_FIXED)
      .value("VARLEN", XPROC_C_CHANNEL_VARLEN)
      .export_values();

  py::enum_<xproc_c_layout_error>(m, "LayoutError")
      .value("NONE", XPROC_C_LAYOUT_ERROR_NONE)
      .value("NOT_ATTACHED", XPROC_C_LAYOUT_ERROR_NOT_ATTACHED)
      .value("BAD_MAGIC", XPROC_C_LAYOUT_ERROR_BAD_MAGIC)
      .value("NOT_READY_TIMEOUT", XPROC_C_LAYOUT_ERROR_NOT_READY_TIMEOUT)
      .value("VERSION_MISMATCH", XPROC_C_LAYOUT_ERROR_VERSION_MISMATCH)
      .value("HEADER_SIZE_MISMATCH", XPROC_C_LAYOUT_ERROR_HEADER_SIZE_MISMATCH)
      .value("LAYOUT_TYPE_MISMATCH", XPROC_C_LAYOUT_ERROR_LAYOUT_TYPE_MISMATCH)
      .value("FIXED_ITEM_SIZE_MISMATCH", XPROC_C_LAYOUT_ERROR_FIXED_ITEM_SIZE_MISMATCH)
      .value("SCHEMA_ID_MISMATCH", XPROC_C_LAYOUT_ERROR_SCHEMA_ID_MISMATCH)
      .value("ALIGNMENT_INVALID", XPROC_C_LAYOUT_ERROR_ALIGNMENT_INVALID)
      .value("CAPACITY_INSUFFICIENT", XPROC_C_LAYOUT_ERROR_CAPACITY_INSUFFICIENT)
      .export_values();

  py::class_<transport_options>(m, "TransportOptions")
      .def(py::init<>())
      .def_readwrite("backend", &transport_options::backend)
      .def_readwrite("path", &transport_options::path)
      .def_readwrite("shm_size", &transport_options::shm_size)
      .def_readwrite("item_size", &transport_options::item_size)
      .def_readwrite("data_align", &transport_options::data_align)
      .def_readwrite("schema_id", &transport_options::schema_id)
      .def_readwrite("create_if_missing", &transport_options::create_if_missing)
      .def_readwrite("channel_type", &transport_options::channel_type)
      .def_readwrite("win32_object_namespace", &transport_options::win32_object_namespace)
      .def_readwrite("socket_host", &transport_options::socket_host)
      .def_readwrite("socket_port", &transport_options::socket_port)
      .def_readwrite("socket_listen", &transport_options::socket_listen)
      .def_readwrite("socket_connect_retries", &transport_options::socket_connect_retries)
      .def_readwrite("socket_connect_retry_ms", &transport_options::socket_connect_retry_ms)
      .def("__repr__", &repr_transport_options);

  py::class_<snapshot>(m, "Snapshot")
      .def_readonly("write_pos", &snapshot::write_pos)
      .def_readonly("read_pos", &snapshot::read_pos)
      .def_readonly("commit_seq", &snapshot::commit_seq)
      .def_readonly("read_wake_seq", &snapshot::read_wake_seq)
      .def_readonly("attach_count", &snapshot::attach_count)
      .def_readonly("producer_pid", &snapshot::producer_pid)
      .def("__repr__", &repr_snapshot);

  py::class_<producer>(m, "Producer")
      .def(py::init<const transport_options&>())
      .def("close", &producer::close)
      .def("options", &producer::options)
      .def("send_fixed_bytes", &producer::send_fixed_bytes)
      .def("send_fixed_sized", &producer::send_fixed_sized)
      .def("send_varlen", &producer::send_varlen)
      .def("socket_port", &producer::socket_port)
      .def(
          "__enter__", [](producer& self) -> producer& { return self; }, py::return_value_policy::reference_internal)
      .def("__exit__", [](producer& self, py::object, py::object, py::object) { self.close(); });

  py::class_<consumer>(m, "Consumer")
      .def(py::init<const transport_options&>())
      .def("close", &consumer::close)
      .def("options", &consumer::options)
      .def("pending_len", &consumer::pending_len)
      .def("poll_copy", &consumer::poll_copy)
      .def("wait", &consumer::wait)
      .def("socket_port", &consumer::socket_port)
      .def(
          "__enter__", [](consumer& self) -> consumer& { return self; }, py::return_value_policy::reference_internal)
      .def("__exit__", [](consumer& self, py::object, py::object, py::object) { self.close(); });

  py::class_<observer>(m, "Observer")
      .def(py::init<const transport_options&>())
      .def("close", &observer::close)
      .def("options", &observer::options)
      .def("snapshot", &observer::get_snapshot)
      .def("peek_copy", &observer::peek_copy)
      .def(
          "__enter__", [](observer& self) -> observer& { return self; }, py::return_value_policy::reference_internal)
      .def("__exit__", [](observer& self, py::object, py::object, py::object) { self.close(); });

  m.attr("INFER_EXISTING_SHM_SIZE") = py::int_(XPROC_C_INFER_EXISTING_SHM_SIZE);

  m.def("shm_size_for_data_capacity",
        [](std::size_t data_capacity) { return xproc_c_shm_size_for_data_capacity(data_capacity); });
  m.def("shm_data_capacity_for_size",
        [](std::size_t shm_size) { return xproc_c_shm_data_capacity_for_size(shm_size); });
  m.def("status_string", [](xproc_c_status status) { return std::string(xproc_c_status_string(status)); });
  m.def("layout_error_string",
        [](xproc_c_layout_error error) { return std::string(xproc_c_layout_error_string(error)); });
  m.def("version_string", []() { return std::string(xproc_c_version_string()); });
  m.def("current_process_id", []() { return xproc_c_current_process_id(); });
  m.def("last_error_message", []() { return std::string(xproc_c_last_error_message()); });
  m.def("last_layout_error", []() { return xproc_c_last_layout_error(); });
  m.def("validate_options_for", [](xproc_c_endpoint_kind kind, const transport_options& options) {
    marshalled_options marshalled(options);
    require_status_ok("validate_options_for", xproc_c_validate_options_for(kind, &marshalled.value));
  });
  m.def("shm_unlink",
        [](const std::string& path) { require_status_ok("shm_unlink", xproc_c_shm_unlink(path.c_str())); });
  m.attr("__version__") = py::str(xproc_c_version_string());
}
