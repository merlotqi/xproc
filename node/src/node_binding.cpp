#include <napi.h>
#include <xproc_c.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace {

constexpr char kClosedHandleMessage[] = "xproc node binding: handle is closed";
constexpr double kMaxSafeInteger = 9007199254740991.0;

struct owned_options {
  xproc_c_options value{};
  std::string path;
  std::string win32_object_namespace;
  std::string socket_host;

  owned_options() { xproc_c_options_init(&value); }
};

struct byte_span {
  const void* data{nullptr};
  std::uint32_t len{0};
};

std::string lowercase_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

bool throw_type_error(Napi::Env env, const std::string& message) {
  Napi::TypeError::New(env, message).ThrowAsJavaScriptException();
  return false;
}

bool throw_range_error(Napi::Env env, const std::string& message) {
  Napi::RangeError::New(env, message).ThrowAsJavaScriptException();
  return false;
}

Napi::Error make_xproc_error(Napi::Env env, xproc_c_status status, const char* context) {
  std::string message;
  if (context != nullptr && context[0] != '\0') {
    message += context;
    message += ": ";
  }

  const char* last_error = xproc_c_last_error_message();
  if (last_error != nullptr && last_error[0] != '\0') {
    message += last_error;
  } else {
    message += xproc_c_status_string(status);
  }

  Napi::Error error = Napi::Error::New(env, message);
  error.Value().Set("status", Napi::Number::New(env, status));
  error.Value().Set("statusCode", Napi::String::New(env, xproc_c_status_string(status)));

  const xproc_c_layout_error layout_error = xproc_c_last_layout_error();
  error.Value().Set("layoutError", Napi::Number::New(env, layout_error));
  error.Value().Set("layoutErrorCode", Napi::String::New(env, xproc_c_layout_error_string(layout_error)));
  return error;
}

bool throw_xproc_status(Napi::Env env, xproc_c_status status, const char* context) {
  make_xproc_error(env, status, context).ThrowAsJavaScriptException();
  return false;
}

bool parse_uint64(Napi::Env env, const Napi::Value& value, const char* field_name, std::uint64_t* out) {
  if (value.IsBigInt()) {
    bool lossless = false;
    const std::uint64_t parsed = value.As<Napi::BigInt>().Uint64Value(&lossless);
    if (!lossless) {
      return throw_range_error(env, std::string(field_name) + " must fit in uint64");
    }
    *out = parsed;
    return true;
  }

  if (!value.IsNumber()) {
    return throw_type_error(env, std::string(field_name) + " must be a number or bigint");
  }

  const double parsed = value.As<Napi::Number>().DoubleValue();
  if (!std::isfinite(parsed) || parsed < 0.0 || std::floor(parsed) != parsed) {
    return throw_range_error(env, std::string(field_name) + " must be a non-negative integer");
  }
  if (parsed > kMaxSafeInteger) {
    return throw_range_error(env, std::string(field_name) + " is too large for a JS number; use a bigint");
  }

  *out = static_cast<std::uint64_t>(parsed);
  return true;
}

bool parse_size_t(Napi::Env env, const Napi::Value& value, const char* field_name, std::size_t* out) {
  std::uint64_t parsed = 0;
  if (!parse_uint64(env, value, field_name, &parsed)) {
    return false;
  }
  if (parsed > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return throw_range_error(env, std::string(field_name) + " does not fit in size_t");
  }
  *out = static_cast<std::size_t>(parsed);
  return true;
}

bool parse_uint32(Napi::Env env, const Napi::Value& value, const char* field_name, std::uint32_t* out) {
  std::uint64_t parsed = 0;
  if (!parse_uint64(env, value, field_name, &parsed)) {
    return false;
  }
  if (parsed > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
    return throw_range_error(env, std::string(field_name) + " must fit in uint32");
  }
  *out = static_cast<std::uint32_t>(parsed);
  return true;
}

bool parse_uint16(Napi::Env env, const Napi::Value& value, const char* field_name, std::uint16_t* out) {
  std::uint32_t parsed = 0;
  if (!parse_uint32(env, value, field_name, &parsed)) {
    return false;
  }
  if (parsed > static_cast<std::uint32_t>(std::numeric_limits<std::uint16_t>::max())) {
    return throw_range_error(env, std::string(field_name) + " must fit in uint16");
  }
  *out = static_cast<std::uint16_t>(parsed);
  return true;
}

bool parse_int(Napi::Env env, const Napi::Value& value, const char* field_name, int* out) {
  if (!value.IsNumber()) {
    return throw_type_error(env, std::string(field_name) + " must be a number");
  }

  const double parsed = value.As<Napi::Number>().DoubleValue();
  if (!std::isfinite(parsed) || std::floor(parsed) != parsed) {
    return throw_range_error(env, std::string(field_name) + " must be an integer");
  }
  if (parsed < static_cast<double>(std::numeric_limits<int>::min()) ||
      parsed > static_cast<double>(std::numeric_limits<int>::max())) {
    return throw_range_error(env, std::string(field_name) + " must fit in int");
  }

  *out = static_cast<int>(parsed);
  return true;
}

bool parse_bool(Napi::Env env, const Napi::Value& value, const char* field_name, int* out) {
  if (value.IsBoolean()) {
    *out = value.As<Napi::Boolean>().Value() ? 1 : 0;
    return true;
  }
  if (value.IsNumber()) {
    const double parsed = value.As<Napi::Number>().DoubleValue();
    if (parsed == 0.0) {
      *out = 0;
      return true;
    }
    if (parsed == 1.0) {
      *out = 1;
      return true;
    }
  }
  return throw_type_error(env, std::string(field_name) + " must be a boolean or 0/1");
}

bool parse_backend(Napi::Env env, const Napi::Value& value, xproc_c_backend* out) {
  if (value.IsNumber()) {
    const std::uint32_t parsed = value.As<Napi::Number>().Uint32Value();
    if (parsed == XPROC_C_BACKEND_SHARED_MEMORY || parsed == XPROC_C_BACKEND_SOCKET) {
      *out = static_cast<xproc_c_backend>(parsed);
      return true;
    }
    return throw_range_error(env, "backend must be XPROC_C_BACKEND_SHARED_MEMORY or XPROC_C_BACKEND_SOCKET");
  }
  if (!value.IsString()) {
    return throw_type_error(env, "backend must be a number or string");
  }

  const std::string parsed = lowercase_ascii(value.As<Napi::String>().Utf8Value());
  if (parsed == "shared_memory" || parsed == "shared-memory" || parsed == "sharedmemory" || parsed == "shm") {
    *out = XPROC_C_BACKEND_SHARED_MEMORY;
    return true;
  }
  if (parsed == "socket" || parsed == "tcp") {
    *out = XPROC_C_BACKEND_SOCKET;
    return true;
  }
  return throw_range_error(env, "backend must be shared_memory/shm or socket");
}

bool parse_channel_type(Napi::Env env, const Napi::Value& value, xproc_c_channel_type* out) {
  if (value.IsNumber()) {
    const std::uint32_t parsed = value.As<Napi::Number>().Uint32Value();
    if (parsed == XPROC_C_CHANNEL_FIXED || parsed == XPROC_C_CHANNEL_VARLEN) {
      *out = static_cast<xproc_c_channel_type>(parsed);
      return true;
    }
    return throw_range_error(env, "channelType must be XPROC_C_CHANNEL_FIXED or XPROC_C_CHANNEL_VARLEN");
  }
  if (!value.IsString()) {
    return throw_type_error(env, "channelType must be a number or string");
  }

  const std::string parsed = lowercase_ascii(value.As<Napi::String>().Utf8Value());
  if (parsed == "fixed") {
    *out = XPROC_C_CHANNEL_FIXED;
    return true;
  }
  if (parsed == "varlen" || parsed == "variable" || parsed == "variable_length" || parsed == "variable-length") {
    *out = XPROC_C_CHANNEL_VARLEN;
    return true;
  }
  return throw_range_error(env, "channelType must be fixed or varlen");
}

bool parse_endpoint_kind(Napi::Env env, const Napi::Value& value, xproc_c_endpoint_kind* out) {
  if (value.IsNumber()) {
    const std::uint32_t parsed = value.As<Napi::Number>().Uint32Value();
    if (parsed == XPROC_C_ENDPOINT_PRODUCER || parsed == XPROC_C_ENDPOINT_CONSUMER ||
        parsed == XPROC_C_ENDPOINT_OBSERVER) {
      *out = static_cast<xproc_c_endpoint_kind>(parsed);
      return true;
    }
    return throw_range_error(
        env,
        "endpoint kind must be XPROC_C_ENDPOINT_PRODUCER, XPROC_C_ENDPOINT_CONSUMER, or XPROC_C_ENDPOINT_OBSERVER");
  }
  if (!value.IsString()) {
    return throw_type_error(env, "endpoint kind must be a number or string");
  }

  const std::string parsed = lowercase_ascii(value.As<Napi::String>().Utf8Value());
  if (parsed == "producer") {
    *out = XPROC_C_ENDPOINT_PRODUCER;
    return true;
  }
  if (parsed == "consumer") {
    *out = XPROC_C_ENDPOINT_CONSUMER;
    return true;
  }
  if (parsed == "observer") {
    *out = XPROC_C_ENDPOINT_OBSERVER;
    return true;
  }
  return throw_range_error(env, "endpoint kind must be producer, consumer, or observer");
}

bool assign_optional_string(Napi::Env env, const Napi::Object& object, const char* property_name, std::string* storage,
                            const char** target, bool allow_null) {
  if (!object.Has(property_name)) {
    return true;
  }

  const Napi::Value value = object.Get(property_name);
  if (value.IsUndefined()) {
    return true;
  }
  if (value.IsNull()) {
    if (!allow_null) {
      return throw_type_error(env, std::string(property_name) + " must be a string");
    }
    storage->clear();
    *target = nullptr;
    return true;
  }
  if (!value.IsString()) {
    return throw_type_error(env, std::string(property_name) + " must be a string");
  }

  *storage = value.As<Napi::String>().Utf8Value();
  *target = storage->c_str();
  return true;
}

bool options_from_js(Napi::Env env, const Napi::Value& value, owned_options* out) {
  if (value.IsUndefined() || value.IsNull()) {
    return true;
  }
  if (!value.IsObject()) {
    return throw_type_error(env, "options must be an object");
  }

  const Napi::Object object = value.As<Napi::Object>();

  if (object.Has("backend") && !parse_backend(env, object.Get("backend"), &out->value.backend)) {
    return false;
  }
  if (!assign_optional_string(env, object, "path", &out->path, &out->value.path, true)) {
    return false;
  }
  if (object.Has("shmSize") && !parse_size_t(env, object.Get("shmSize"), "shmSize", &out->value.shm_size)) {
    return false;
  }
  if (object.Has("itemSize") && !parse_uint32(env, object.Get("itemSize"), "itemSize", &out->value.item_size)) {
    return false;
  }
  if (object.Has("dataAlign") && !parse_uint32(env, object.Get("dataAlign"), "dataAlign", &out->value.data_align)) {
    return false;
  }
  if (object.Has("schemaId") && !parse_uint64(env, object.Get("schemaId"), "schemaId", &out->value.schema_id)) {
    return false;
  }
  if (object.Has("creatorTimestampNs") &&
      !parse_uint64(env, object.Get("creatorTimestampNs"), "creatorTimestampNs", &out->value.creator_timestamp_ns)) {
    return false;
  }
  if (object.Has("creatorFlags") &&
      !parse_uint64(env, object.Get("creatorFlags"), "creatorFlags", &out->value.creator_flags)) {
    return false;
  }
  if (object.Has("createIfMissing") &&
      !parse_bool(env, object.Get("createIfMissing"), "createIfMissing", &out->value.create_if_missing)) {
    return false;
  }

  const bool has_channel_type = object.Has("channelType");
  const bool has_type_alias = object.Has("type");
  if (has_channel_type && has_type_alias) {
    return throw_type_error(env, "options must use only one of channelType or type");
  }
  if (has_channel_type && !parse_channel_type(env, object.Get("channelType"), &out->value.channel_type)) {
    return false;
  }
  if (has_type_alias && !parse_channel_type(env, object.Get("type"), &out->value.channel_type)) {
    return false;
  }

  if (!assign_optional_string(env, object, "win32ObjectNamespace", &out->win32_object_namespace,
                              &out->value.win32_object_namespace, true)) {
    return false;
  }
  if (!assign_optional_string(env, object, "socketHost", &out->socket_host, &out->value.socket_host, true)) {
    return false;
  }
  if (object.Has("socketPort") && !parse_uint16(env, object.Get("socketPort"), "socketPort", &out->value.socket_port)) {
    return false;
  }
  if (object.Has("socketListen") &&
      !parse_bool(env, object.Get("socketListen"), "socketListen", &out->value.socket_listen)) {
    return false;
  }
  if (object.Has("socketConnectRetries") &&
      !parse_int(env, object.Get("socketConnectRetries"), "socketConnectRetries", &out->value.socket_connect_retries)) {
    return false;
  }
  if (object.Has("socketConnectRetryMs") && !parse_int(env, object.Get("socketConnectRetryMs"), "socketConnectRetryMs",
                                                       &out->value.socket_connect_retry_ms)) {
    return false;
  }

  return true;
}

Napi::Value js_bigint_from_size(Napi::Env env, std::size_t value) {
  return Napi::BigInt::New(env, static_cast<std::uint64_t>(value));
}

Napi::Value js_bigint_from_u64(Napi::Env env, std::uint64_t value) { return Napi::BigInt::New(env, value); }

Napi::Object options_to_js(Napi::Env env, const xproc_c_options& options) {
  Napi::Object object = Napi::Object::New(env);
  object.Set("backend", Napi::Number::New(env, options.backend));
  object.Set("path", options.path != nullptr ? Napi::Value(Napi::String::New(env, options.path)) : env.Undefined());
  object.Set("shmSize", js_bigint_from_size(env, options.shm_size));
  object.Set("itemSize", Napi::Number::New(env, options.item_size));
  object.Set("dataAlign", Napi::Number::New(env, options.data_align));
  object.Set("schemaId", js_bigint_from_u64(env, options.schema_id));
  object.Set("creatorTimestampNs", js_bigint_from_u64(env, options.creator_timestamp_ns));
  object.Set("creatorFlags", js_bigint_from_u64(env, options.creator_flags));
  object.Set("createIfMissing", Napi::Boolean::New(env, options.create_if_missing != 0));
  object.Set("channelType", Napi::Number::New(env, options.channel_type));
  object.Set("type", Napi::Number::New(env, options.channel_type));
  object.Set("win32ObjectNamespace", options.win32_object_namespace != nullptr
                                         ? Napi::Value(Napi::String::New(env, options.win32_object_namespace))
                                         : env.Undefined());
  object.Set("socketHost", options.socket_host != nullptr ? Napi::Value(Napi::String::New(env, options.socket_host))
                                                          : env.Undefined());
  object.Set("socketPort", Napi::Number::New(env, options.socket_port));
  object.Set("socketListen", Napi::Boolean::New(env, options.socket_listen != 0));
  object.Set("socketConnectRetries", Napi::Number::New(env, options.socket_connect_retries));
  object.Set("socketConnectRetryMs", Napi::Number::New(env, options.socket_connect_retry_ms));
  return object;
}

Napi::Object snapshot_to_js(Napi::Env env, const xproc_c_snapshot& snapshot) {
  Napi::Object object = Napi::Object::New(env);
  object.Set("writePos", js_bigint_from_u64(env, snapshot.write_pos));
  object.Set("readPos", js_bigint_from_u64(env, snapshot.read_pos));
  object.Set("commitSeq", Napi::Number::New(env, snapshot.commit_seq));
  object.Set("readWakeSeq", Napi::Number::New(env, snapshot.read_wake_seq));
  object.Set("attachCount", Napi::Number::New(env, snapshot.attach_count));
  object.Set("producerPid", Napi::Number::New(env, snapshot.producer_pid));
  return object;
}

bool byte_span_from_js(Napi::Env env, const Napi::Value& value, const char* field_name, byte_span* out) {
  if (value.IsBuffer()) {
    const Napi::Buffer<std::uint8_t> buffer = value.As<Napi::Buffer<std::uint8_t>>();
    if (buffer.Length() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
      return throw_range_error(env, std::string(field_name) + " length must fit in uint32");
    }
    out->data = buffer.Data();
    out->len = static_cast<std::uint32_t>(buffer.Length());
    return true;
  }

  if (value.IsTypedArray()) {
    const Napi::TypedArray view = value.As<Napi::TypedArray>();
    if (view.ByteLength() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
      return throw_range_error(env, std::string(field_name) + " length must fit in uint32");
    }
    Napi::ArrayBuffer backing = view.ArrayBuffer();
    out->data = static_cast<const std::uint8_t*>(backing.Data()) + view.ByteOffset();
    out->len = static_cast<std::uint32_t>(view.ByteLength());
    return true;
  }

  if (value.IsArrayBuffer()) {
    Napi::ArrayBuffer buffer = value.As<Napi::ArrayBuffer>();
    if (buffer.ByteLength() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
      return throw_range_error(env, std::string(field_name) + " length must fit in uint32");
    }
    out->data = buffer.Data();
    out->len = static_cast<std::uint32_t>(buffer.ByteLength());
    return true;
  }

  return throw_type_error(env, std::string(field_name) + " must be a Buffer, TypedArray, or ArrayBuffer");
}

Napi::Value empty_buffer(Napi::Env env) { return Napi::Buffer<std::uint8_t>::New(env, 0); }

template <typename Handle>
bool ensure_open(Napi::Env env, Handle* handle) {
  if (handle != nullptr) {
    return true;
  }
  Napi::Error::New(env, kClosedHandleMessage).ThrowAsJavaScriptException();
  return false;
}

class producer_wrap final : public Napi::ObjectWrap<producer_wrap> {
 public:
  static Napi::Function init(Napi::Env env) {
    return DefineClass(env, "Producer",
                       {
                           InstanceMethod("close", &producer_wrap::close),
                           InstanceMethod("options", &producer_wrap::options),
                           InstanceMethod("sendFixedBytes", &producer_wrap::send_fixed_bytes),
                           InstanceMethod("sendFixedSized", &producer_wrap::send_fixed_sized),
                           InstanceMethod("sendVarlen", &producer_wrap::send_varlen),
                           InstanceMethod("socketPort", &producer_wrap::socket_port),
                       });
  }

  explicit producer_wrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<producer_wrap>(info) {
    Napi::Env env = info.Env();
    owned_options options;
    if (info.Length() > 0 && !options_from_js(env, info[0], &options)) {
      return;
    }

    const xproc_c_status status = xproc_c_producer_open(&options.value, &handle_);
    if (status != XPROC_C_STATUS_OK) {
      handle_ = nullptr;
      throw_xproc_status(env, status, "Producer");
    }
  }

  ~producer_wrap() override { reset(); }

 private:
  void reset() {
    if (handle_ != nullptr) {
      xproc_c_producer_close(handle_);
      handle_ = nullptr;
    }
  }

  Napi::Value close(const Napi::CallbackInfo& info) {
    reset();
    return info.Env().Undefined();
  }

  Napi::Value options(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!ensure_open(env, handle_)) {
      return env.Undefined();
    }

    xproc_c_options options{};
    const xproc_c_status status = xproc_c_producer_options(handle_, &options);
    if (status != XPROC_C_STATUS_OK) {
      throw_xproc_status(env, status, "Producer.options");
      return env.Undefined();
    }
    return options_to_js(env, options);
  }

  Napi::Value send_fixed_bytes(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!ensure_open(env, handle_)) {
      return env.Undefined();
    }
    if (info.Length() < 1) {
      throw_type_error(env, "Producer.sendFixedBytes requires one byte source argument");
      return env.Undefined();
    }

    byte_span bytes;
    if (!byte_span_from_js(env, info[0], "data", &bytes)) {
      return env.Undefined();
    }

    const xproc_c_status status = xproc_c_producer_send_fixed_bytes(handle_, bytes.data, bytes.len);
    if (status != XPROC_C_STATUS_OK) {
      throw_xproc_status(env, status, "Producer.sendFixedBytes");
    }
    return env.Undefined();
  }

  Napi::Value send_fixed_sized(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!ensure_open(env, handle_)) {
      return env.Undefined();
    }
    if (info.Length() < 1) {
      throw_type_error(env, "Producer.sendFixedSized requires one byte source argument");
      return env.Undefined();
    }

    byte_span bytes;
    if (!byte_span_from_js(env, info[0], "data", &bytes)) {
      return env.Undefined();
    }

    const xproc_c_status status = xproc_c_producer_send_fixed_sized(handle_, bytes.data, bytes.len);
    if (status != XPROC_C_STATUS_OK) {
      throw_xproc_status(env, status, "Producer.sendFixedSized");
    }
    return env.Undefined();
  }

  Napi::Value send_varlen(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!ensure_open(env, handle_)) {
      return env.Undefined();
    }
    if (info.Length() < 1) {
      throw_type_error(env, "Producer.sendVarlen requires one byte source argument");
      return env.Undefined();
    }

    byte_span bytes;
    if (!byte_span_from_js(env, info[0], "data", &bytes)) {
      return env.Undefined();
    }

    const xproc_c_status status = xproc_c_producer_send_varlen(handle_, bytes.data, bytes.len);
    if (status != XPROC_C_STATUS_OK) {
      throw_xproc_status(env, status, "Producer.sendVarlen");
    }
    return env.Undefined();
  }

  Napi::Value socket_port(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!ensure_open(env, handle_)) {
      return env.Undefined();
    }

    std::uint16_t port = 0;
    const xproc_c_status status = xproc_c_producer_socket_port(handle_, &port);
    if (status != XPROC_C_STATUS_OK) {
      throw_xproc_status(env, status, "Producer.socketPort");
      return env.Undefined();
    }
    return Napi::Number::New(env, port);
  }

  xproc_c_producer* handle_{nullptr};
};

class consumer_wrap final : public Napi::ObjectWrap<consumer_wrap> {
 public:
  static Napi::Function init(Napi::Env env) {
    return DefineClass(env, "Consumer",
                       {
                           InstanceMethod("close", &consumer_wrap::close),
                           InstanceMethod("options", &consumer_wrap::options),
                           InstanceMethod("pendingLen", &consumer_wrap::pending_len),
                           InstanceMethod("pollCopy", &consumer_wrap::poll_copy),
                           InstanceMethod("wait", &consumer_wrap::wait),
                           InstanceMethod("socketPort", &consumer_wrap::socket_port),
                       });
  }

  explicit consumer_wrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<consumer_wrap>(info) {
    Napi::Env env = info.Env();
    owned_options options;
    if (info.Length() > 0 && !options_from_js(env, info[0], &options)) {
      return;
    }

    const xproc_c_status status = xproc_c_consumer_open(&options.value, &handle_);
    if (status != XPROC_C_STATUS_OK) {
      handle_ = nullptr;
      throw_xproc_status(env, status, "Consumer");
    }
  }

  ~consumer_wrap() override { reset(); }

 private:
  void reset() {
    if (handle_ != nullptr) {
      xproc_c_consumer_close(handle_);
      handle_ = nullptr;
    }
  }

  Napi::Value close(const Napi::CallbackInfo& info) {
    reset();
    return info.Env().Undefined();
  }

  Napi::Value options(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!ensure_open(env, handle_)) {
      return env.Undefined();
    }

    xproc_c_options options{};
    const xproc_c_status status = xproc_c_consumer_options(handle_, &options);
    if (status != XPROC_C_STATUS_OK) {
      throw_xproc_status(env, status, "Consumer.options");
      return env.Undefined();
    }
    return options_to_js(env, options);
  }

  Napi::Value pending_len(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!ensure_open(env, handle_)) {
      return env.Undefined();
    }

    std::uint32_t pending_len = 0;
    const xproc_c_status status = xproc_c_consumer_pending_len(handle_, &pending_len);
    if (status != XPROC_C_STATUS_OK) {
      throw_xproc_status(env, status, "Consumer.pendingLen");
      return env.Undefined();
    }
    return Napi::Number::New(env, pending_len);
  }

  Napi::Value poll_copy(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!ensure_open(env, handle_)) {
      return env.Undefined();
    }

    std::uint32_t required_len = 0;
    xproc_c_status status = xproc_c_consumer_poll_copy(handle_, nullptr, 0, &required_len);
    if (status == XPROC_C_STATUS_AGAIN) {
      return env.Null();
    }
    if (status == XPROC_C_STATUS_OK) {
      return empty_buffer(env);
    }
    if (status != XPROC_C_STATUS_BUFFER_TOO_SMALL) {
      throw_xproc_status(env, status, "Consumer.pollCopy");
      return env.Undefined();
    }

    Napi::Buffer<std::uint8_t> buffer = Napi::Buffer<std::uint8_t>::New(env, required_len);
    status =
        xproc_c_consumer_poll_copy(handle_, required_len == 0 ? nullptr : buffer.Data(), required_len, &required_len);
    if (status != XPROC_C_STATUS_OK) {
      throw_xproc_status(env, status, "Consumer.pollCopy");
      return env.Undefined();
    }
    return buffer;
  }

  Napi::Value wait(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!ensure_open(env, handle_)) {
      return env.Undefined();
    }

    const xproc_c_status status = xproc_c_consumer_wait(handle_);
    if (status != XPROC_C_STATUS_OK) {
      throw_xproc_status(env, status, "Consumer.wait");
    }
    return env.Undefined();
  }

  Napi::Value socket_port(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!ensure_open(env, handle_)) {
      return env.Undefined();
    }

    std::uint16_t port = 0;
    const xproc_c_status status = xproc_c_consumer_socket_port(handle_, &port);
    if (status != XPROC_C_STATUS_OK) {
      throw_xproc_status(env, status, "Consumer.socketPort");
      return env.Undefined();
    }
    return Napi::Number::New(env, port);
  }

  xproc_c_consumer* handle_{nullptr};
};

class observer_wrap final : public Napi::ObjectWrap<observer_wrap> {
 public:
  static Napi::Function init(Napi::Env env) {
    return DefineClass(env, "Observer",
                       {
                           InstanceMethod("close", &observer_wrap::close),
                           InstanceMethod("options", &observer_wrap::options),
                           InstanceMethod("snapshot", &observer_wrap::snapshot),
                           InstanceMethod("peekCopy", &observer_wrap::peek_copy),
                       });
  }

  explicit observer_wrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<observer_wrap>(info) {
    Napi::Env env = info.Env();
    owned_options options;
    if (info.Length() > 0 && !options_from_js(env, info[0], &options)) {
      return;
    }

    const xproc_c_status status = xproc_c_observer_open(&options.value, &handle_);
    if (status != XPROC_C_STATUS_OK) {
      handle_ = nullptr;
      throw_xproc_status(env, status, "Observer");
    }
  }

  ~observer_wrap() override { reset(); }

 private:
  void reset() {
    if (handle_ != nullptr) {
      xproc_c_observer_close(handle_);
      handle_ = nullptr;
    }
  }

  Napi::Value close(const Napi::CallbackInfo& info) {
    reset();
    return info.Env().Undefined();
  }

  Napi::Value options(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!ensure_open(env, handle_)) {
      return env.Undefined();
    }

    xproc_c_options options{};
    const xproc_c_status status = xproc_c_observer_options(handle_, &options);
    if (status != XPROC_C_STATUS_OK) {
      throw_xproc_status(env, status, "Observer.options");
      return env.Undefined();
    }
    return options_to_js(env, options);
  }

  Napi::Value snapshot(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!ensure_open(env, handle_)) {
      return env.Undefined();
    }

    xproc_c_snapshot snapshot{};
    const xproc_c_status status = xproc_c_observer_snapshot(handle_, &snapshot);
    if (status != XPROC_C_STATUS_OK) {
      throw_xproc_status(env, status, "Observer.snapshot");
      return env.Undefined();
    }
    return snapshot_to_js(env, snapshot);
  }

  Napi::Value peek_copy(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!ensure_open(env, handle_)) {
      return env.Undefined();
    }

    std::uint32_t required_len = 0;
    xproc_c_status status = xproc_c_observer_peek_copy(handle_, nullptr, 0, &required_len);
    if (status == XPROC_C_STATUS_AGAIN) {
      return env.Null();
    }
    if (status == XPROC_C_STATUS_OK) {
      return empty_buffer(env);
    }
    if (status != XPROC_C_STATUS_BUFFER_TOO_SMALL) {
      throw_xproc_status(env, status, "Observer.peekCopy");
      return env.Undefined();
    }

    Napi::Buffer<std::uint8_t> buffer = Napi::Buffer<std::uint8_t>::New(env, required_len);
    status =
        xproc_c_observer_peek_copy(handle_, required_len == 0 ? nullptr : buffer.Data(), required_len, &required_len);
    if (status != XPROC_C_STATUS_OK) {
      throw_xproc_status(env, status, "Observer.peekCopy");
      return env.Undefined();
    }
    return buffer;
  }

  xproc_c_observer* handle_{nullptr};
};

Napi::Value shm_size_for_data_capacity_callback(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    throw_type_error(env, "shmSizeForDataCapacity requires a dataCapacity argument");
    return env.Undefined();
  }

  std::size_t data_capacity = 0;
  if (!parse_size_t(env, info[0], "dataCapacity", &data_capacity)) {
    return env.Undefined();
  }
  return js_bigint_from_size(env, xproc_c_shm_size_for_data_capacity(data_capacity));
}

Napi::Value shm_data_capacity_for_size_callback(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    throw_type_error(env, "shmDataCapacityForSize requires a shmSize argument");
    return env.Undefined();
  }

  std::size_t shm_size = 0;
  if (!parse_size_t(env, info[0], "shmSize", &shm_size)) {
    return env.Undefined();
  }
  return js_bigint_from_size(env, xproc_c_shm_data_capacity_for_size(shm_size));
}

Napi::Value status_string_callback(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    throw_type_error(env, "statusString requires a status argument");
    return env.Undefined();
  }

  std::uint32_t status = 0;
  if (!parse_uint32(env, info[0], "status", &status)) {
    return env.Undefined();
  }
  return Napi::String::New(env, xproc_c_status_string(static_cast<xproc_c_status>(status)));
}

Napi::Value layout_error_string_callback(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    throw_type_error(env, "layoutErrorString requires a layoutError argument");
    return env.Undefined();
  }

  std::uint32_t layout_error = 0;
  if (!parse_uint32(env, info[0], "layoutError", &layout_error)) {
    return env.Undefined();
  }
  return Napi::String::New(env, xproc_c_layout_error_string(static_cast<xproc_c_layout_error>(layout_error)));
}

Napi::Value version_string_callback(const Napi::CallbackInfo& info) {
  return Napi::String::New(info.Env(), xproc_c_version_string());
}

Napi::Value current_process_id_callback(const Napi::CallbackInfo& info) {
  return Napi::Number::New(info.Env(), xproc_c_current_process_id());
}

Napi::Value validate_options_for_callback(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2) {
    throw_type_error(env, "validateOptionsFor requires kind and options arguments");
    return env.Undefined();
  }

  xproc_c_endpoint_kind kind = XPROC_C_ENDPOINT_PRODUCER;
  if (!parse_endpoint_kind(env, info[0], &kind)) {
    return env.Undefined();
  }

  owned_options options;
  if (!options_from_js(env, info[1], &options)) {
    return env.Undefined();
  }

  const xproc_c_status status = xproc_c_validate_options_for(kind, &options.value);
  if (status != XPROC_C_STATUS_OK) {
    throw_xproc_status(env, status, "validateOptionsFor");
    return env.Undefined();
  }
  return Napi::Boolean::New(env, true);
}

Napi::Value shm_unlink_callback(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsString()) {
    throw_type_error(env, "shmUnlink requires a path string");
    return env.Undefined();
  }

  const std::string path = info[0].As<Napi::String>().Utf8Value();
  const xproc_c_status status = xproc_c_shm_unlink(path.c_str());
  if (status != XPROC_C_STATUS_OK) {
    throw_xproc_status(env, status, "shmUnlink");
    return env.Undefined();
  }
  return env.Undefined();
}

Napi::Value read_existing_shm_options_callback(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsString()) {
    throw_type_error(env, "_readExistingShmOptions requires a path string");
    return env.Undefined();
  }

  const std::string path = info[0].As<Napi::String>().Utf8Value();
  std::string win32_namespace_storage;
  const char* win32_namespace = nullptr;
  if (info.Length() > 1 && !info[1].IsUndefined() && !info[1].IsNull()) {
    if (!info[1].IsString()) {
      throw_type_error(env, "_readExistingShmOptions win32ObjectNamespace must be a string when provided");
      return env.Undefined();
    }
    win32_namespace_storage = info[1].As<Napi::String>().Utf8Value();
    win32_namespace = win32_namespace_storage.c_str();
  }

  xproc_c_options options{};
  const xproc_c_status status = xproc_c_shm_read_existing_options(path.c_str(), win32_namespace, &options);
  if (status != XPROC_C_STATUS_OK) {
    throw_xproc_status(env, status, "_readExistingShmOptions");
    return env.Undefined();
  }
  return options_to_js(env, options);
}

void export_constants(Napi::Env env, Napi::Object exports) {
  exports.Set("XPROC_C_STATUS_OK", Napi::Number::New(env, XPROC_C_STATUS_OK));
  exports.Set("XPROC_C_STATUS_AGAIN", Napi::Number::New(env, XPROC_C_STATUS_AGAIN));
  exports.Set("XPROC_C_STATUS_BUFFER_TOO_SMALL", Napi::Number::New(env, XPROC_C_STATUS_BUFFER_TOO_SMALL));
  exports.Set("XPROC_C_STATUS_INVALID_ARGUMENT", Napi::Number::New(env, XPROC_C_STATUS_INVALID_ARGUMENT));
  exports.Set("XPROC_C_STATUS_LOGIC_ERROR", Napi::Number::New(env, XPROC_C_STATUS_LOGIC_ERROR));
  exports.Set("XPROC_C_STATUS_LAYOUT_ERROR", Napi::Number::New(env, XPROC_C_STATUS_LAYOUT_ERROR));
  exports.Set("XPROC_C_STATUS_RUNTIME_ERROR", Napi::Number::New(env, XPROC_C_STATUS_RUNTIME_ERROR));
  exports.Set("XPROC_C_STATUS_NO_MEMORY", Napi::Number::New(env, XPROC_C_STATUS_NO_MEMORY));
  exports.Set("XPROC_C_STATUS_INTERNAL_ERROR", Napi::Number::New(env, XPROC_C_STATUS_INTERNAL_ERROR));

  exports.Set("XPROC_C_ENDPOINT_PRODUCER", Napi::Number::New(env, XPROC_C_ENDPOINT_PRODUCER));
  exports.Set("XPROC_C_ENDPOINT_CONSUMER", Napi::Number::New(env, XPROC_C_ENDPOINT_CONSUMER));
  exports.Set("XPROC_C_ENDPOINT_OBSERVER", Napi::Number::New(env, XPROC_C_ENDPOINT_OBSERVER));

  exports.Set("XPROC_C_BACKEND_SHARED_MEMORY", Napi::Number::New(env, XPROC_C_BACKEND_SHARED_MEMORY));
  exports.Set("XPROC_C_BACKEND_SOCKET", Napi::Number::New(env, XPROC_C_BACKEND_SOCKET));

  exports.Set("XPROC_C_CHANNEL_FIXED", Napi::Number::New(env, XPROC_C_CHANNEL_FIXED));
  exports.Set("XPROC_C_CHANNEL_VARLEN", Napi::Number::New(env, XPROC_C_CHANNEL_VARLEN));
  exports.Set("XPROC_C_INFER_EXISTING_SHM_SIZE", Napi::Number::New(env, XPROC_C_INFER_EXISTING_SHM_SIZE));

  exports.Set("XPROC_C_LAYOUT_ERROR_NONE", Napi::Number::New(env, XPROC_C_LAYOUT_ERROR_NONE));
  exports.Set("XPROC_C_LAYOUT_ERROR_NOT_ATTACHED", Napi::Number::New(env, XPROC_C_LAYOUT_ERROR_NOT_ATTACHED));
  exports.Set("XPROC_C_LAYOUT_ERROR_BAD_MAGIC", Napi::Number::New(env, XPROC_C_LAYOUT_ERROR_BAD_MAGIC));
  exports.Set("XPROC_C_LAYOUT_ERROR_NOT_READY_TIMEOUT", Napi::Number::New(env, XPROC_C_LAYOUT_ERROR_NOT_READY_TIMEOUT));
  exports.Set("XPROC_C_LAYOUT_ERROR_VERSION_MISMATCH", Napi::Number::New(env, XPROC_C_LAYOUT_ERROR_VERSION_MISMATCH));
  exports.Set("XPROC_C_LAYOUT_ERROR_HEADER_SIZE_MISMATCH",
              Napi::Number::New(env, XPROC_C_LAYOUT_ERROR_HEADER_SIZE_MISMATCH));
  exports.Set("XPROC_C_LAYOUT_ERROR_LAYOUT_TYPE_MISMATCH",
              Napi::Number::New(env, XPROC_C_LAYOUT_ERROR_LAYOUT_TYPE_MISMATCH));
  exports.Set("XPROC_C_LAYOUT_ERROR_FIXED_ITEM_SIZE_MISMATCH",
              Napi::Number::New(env, XPROC_C_LAYOUT_ERROR_FIXED_ITEM_SIZE_MISMATCH));
  exports.Set("XPROC_C_LAYOUT_ERROR_SCHEMA_ID_MISMATCH",
              Napi::Number::New(env, XPROC_C_LAYOUT_ERROR_SCHEMA_ID_MISMATCH));
  exports.Set("XPROC_C_LAYOUT_ERROR_ALIGNMENT_INVALID", Napi::Number::New(env, XPROC_C_LAYOUT_ERROR_ALIGNMENT_INVALID));
  exports.Set("XPROC_C_LAYOUT_ERROR_CAPACITY_INSUFFICIENT",
              Napi::Number::New(env, XPROC_C_LAYOUT_ERROR_CAPACITY_INSUFFICIENT));
}

}  // namespace

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  export_constants(env, exports);

  exports.Set("shmSizeForDataCapacity", Napi::Function::New(env, shm_size_for_data_capacity_callback));
  exports.Set("shmDataCapacityForSize", Napi::Function::New(env, shm_data_capacity_for_size_callback));
  exports.Set("statusString", Napi::Function::New(env, status_string_callback));
  exports.Set("layoutErrorString", Napi::Function::New(env, layout_error_string_callback));
  exports.Set("versionString", Napi::Function::New(env, version_string_callback));
  exports.Set("currentProcessId", Napi::Function::New(env, current_process_id_callback));
  exports.Set("validateOptionsFor", Napi::Function::New(env, validate_options_for_callback));
  exports.Set("shmUnlink", Napi::Function::New(env, shm_unlink_callback));
  exports.Set("_readExistingShmOptions", Napi::Function::New(env, read_existing_shm_options_callback));

  exports.Set("Producer", producer_wrap::init(env));
  exports.Set("Consumer", consumer_wrap::init(env));
  exports.Set("Observer", observer_wrap::init(env));

  return exports;
}

NODE_API_MODULE(xproc, Init)
