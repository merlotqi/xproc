#pragma once

// ipc include files.
#include <xproc/ipc/channel.hpp>
#include <xproc/ipc/channel_interface.hpp>
#include <xproc/ipc/codec_exception.hpp>
#include <xproc/ipc/endpoint.hpp>
#include <xproc/ipc/inspector.hpp>
#include <xproc/ipc/messaging.hpp>
#include <xproc/ipc/observer.hpp>
#include <xproc/ipc/options.hpp>
#include <xproc/ipc/runtime.hpp>
#include <xproc/ipc/send_result.hpp>
#include <xproc/ipc/shm_builders.hpp>
#include <xproc/ipc/socket_channel.hpp>
#include <xproc/ipc/transport_factory.hpp>

// platform include files.
#include <xproc/platform/platform.hpp>
#include <xproc/platform/process.hpp>

// protocol include files.
#include <xproc/protocol/codec_traits.hpp>
#include <xproc/protocol/codecs.hpp>
#include <xproc/protocol/json_codec_stub.hpp>
#include <xproc/protocol/protobuf_stub.hpp>
#include <xproc/protocol/protocol.hpp>

// ringbuffer include files.
#include <xproc/ringbuffer/fixed_reader.hpp>
#include <xproc/ringbuffer/fixed_writer.hpp>
#include <xproc/ringbuffer/iringbuffer.hpp>
#include <xproc/ringbuffer/reserve_result.hpp>
#include <xproc/ringbuffer/ringbuffer_error.hpp>
#include <xproc/ringbuffer/varlen_reader.hpp>
#include <xproc/ringbuffer/varlen_writer.hpp>

// shm include files.
#include <xproc/core/layout_exception.hpp>
#include <xproc/core/shm.hpp>
#include <xproc/core/shm_backend.hpp>
#include <xproc/core/shm_layout.hpp>
#include <xproc/core/shm_layout_manager.hpp>
#include <xproc/core/shm_open_mode.hpp>

// sync include files.
#include <xproc/sync/atomic_backoff.hpp>
#include <xproc/sync/atomic_wait.hpp>

namespace xproc {

using namespace ipc;
using namespace platform;
using namespace protocol;
using namespace ringbuffer;
using namespace core;
using namespace sync;

}  // namespace xproc
