#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace xproc {
namespace sync {

template<typename T>
void atomic_await(const std::atomic<T> *atomic, T old);

template<typename T>
void atomic_notify_one(const std::atomic<T> *atomic);

template<typename T>
void atomic_notify_all(const std::atomic<T> *atomic);

}// namespace sync
}// namespace xproc
