#pragma once

#include <algorithm>
#include <cstdint>
#include <thread>
#include <xproc/platform/platform.hpp>
#include <xproc/sync/atomic_wait.hpp>

namespace xproc::sync {

// Exponential backoff: spin delay doubles each iteration (1,2,4,...,256),
// then yields a few times, then blocks via atomic_wait.
//
// Total pauses across 12 default spin iterations: 1+2+4+8+16+32+64+128+256+256+256+256 ≈ 1279.
// This reduces cache-line contention under light contention (fast hand-off) while
// backing off aggressively under heavy contention (many spinners).
class atomic_backoff {
 public:
  // Number of spin iterations before yielding.
  static constexpr uint32_t default_spin_iterations = 12;
  // Max pause count per iteration (2^8 = 256).
  static constexpr uint32_t max_pause_exponent = 8;
  static constexpr uint32_t max_pauses_per_iteration = 1u << max_pause_exponent;
  // Number of yield iterations before blocking.
  static constexpr uint32_t default_yield_iterations = 10;

  explicit atomic_backoff(uint32_t spin_iterations = default_spin_iterations)
      : spin_iterations_(spin_iterations), iterations_(0) {}

  template <typename T>
  void pause(const std::atomic<T>& atomic, T old) {
    iterations_++;
    if (iterations_ <= spin_iterations_) {
      // Exponential delay: 1, 2, 4, 8, ..., capped at 256 pauses.
      const uint32_t exp = (std::min)(iterations_ - 1, max_pause_exponent);
      const uint32_t delay = 1u << exp;
      for (uint32_t i = 0; i < delay; ++i) {
        XPROC_CPU_PAUSE();
      }
    } else if (iterations_ <= spin_iterations_ + default_yield_iterations) {
      std::this_thread::yield();
    } else {
      atomic_wait(&atomic, old);
      reset();
    }
  }

  void reset() { iterations_ = 0; }

 private:
  uint32_t spin_iterations_;
  uint32_t iterations_;
};

}  // namespace xproc::sync
