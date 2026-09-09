#pragma once

// ipc include files.
#include <xproc/ipc/channel.hpp>
#include <xproc/ipc/channel_interface.hpp>
#include <xproc/ipc/endpoint.hpp>
#include <xproc/ipc/inspector.hpp>
#include <xproc/ipc/message_meta.hpp>
#include <xproc/ipc/observer.hpp>
#include <xproc/ipc/options.hpp>
#include <xproc/ipc/runtime.hpp>
#include <xproc/ipc/send_result.hpp>
#include <xproc/ipc/shm_builders.hpp>
#include <xproc/ipc/shm_channel.hpp>
#include <xproc/ipc/socket_builders.hpp>
#include <xproc/ipc/socket_channel.hpp>
#include <xproc/ipc/transport_factory.hpp>

// platform include files.
#include <xproc/platform/platform.hpp>
#include <xproc/platform/process.hpp>

// spscring (ringbuffer replacement).
#include <spscring/spscring.hpp>

// shm include files.
#include <xproc/core/layout_exception.hpp>
#include <xproc/core/layout_manager.hpp>
#include <xproc/core/layout_types.hpp>
#include <xproc/core/shm.hpp>
#include <xproc/core/shm_open_mode.hpp>

namespace xproc {

using namespace ipc;
using namespace platform;
using namespace core;

}  // namespace xproc
