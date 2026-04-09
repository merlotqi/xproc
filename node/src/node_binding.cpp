#include <napi.h>
#include <xproc_c.h>

using namespace Napi;

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  
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

  exports.Set("XPROC_C_LAYOUT_ERROR_NONE", Napi::Number::New(env, XPROC_C_LAYOUT_ERROR_NONE));
  exports.Set("XPROC_C_LAYOUT_ERROR_NOT_ATTACHED", Napi::Number::New(env, XPROC_C_LAYOUT_ERROR_NOT_ATTACHED));
  exports.Set("XPROC_C_LAYOUT_ERROR_BAD_MAGIC", Napi::Number::New(env, XPROC_C_LAYOUT_ERROR_BAD_MAGIC));
  exports.Set("XPROC_C_LAYOUT_ERROR_NOT_READY_TIMEOUT", Napi::Number::New(env, XPROC_C_LAYOUT_ERROR_NOT_READY_TIMEOUT));
  exports.Set("XPROC_C_LAYOUT_ERROR_VERSION_MISMATCH", Napi::Number::New(env, XPROC_C_LAYOUT_ERROR_VERSION_MISMATCH));
  exports.Set("XPROC_C_LAYOUT_ERROR_HEADER_SIZE_MISMATCH",
              Napi::Number::New(env, XPROC_C_LAYOUT_ERROR_HEADER_SIZE_MISMATCH));
  exports.Set("XPROC_C_LAYOUT_ERROR_LAYOUT_TYPE_MISMATCH",
              Napi::Number::New(env, XPROC_C_LAYOUT_ERROR_LAYOUT_TYPE_MISMATCH));
  exports.Set("XPROC_C_LAYOUT_ERROR_ALIGNMENT_INVALID", Napi::Number::New(env, XPROC_C_LAYOUT_ERROR_ALIGNMENT_INVALID));
  exports.Set("XPROC_C_LAYOUT_ERROR_CAPACITY_INSUFFICIENT",
              Napi::Number::New(env, XPROC_C_LAYOUT_ERROR_CAPACITY_INSUFFICIENT));

  return exports;
}
