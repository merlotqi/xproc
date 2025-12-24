#pragma once

#if defined(__linux__) || defined(__linux)
#define XPROC_PLATFORM_LINUX 1
#define XPROC_OS_NAME "Linux"
#elif defined(_WIN32) || defined(_WIN64)
#define XPROC_PLATFORM_WINDOWS 1
#define XPROC_OS_NAME "Windows"
#else
#error "unsupported platform"
#endif

#if defined(__clang__)
#define XPROC_COMPILER_CLANG 1
#elif defined(__GNU__) || defined(__GNUC__)
#define XPROC_COMPILER_GCC 1
#elif defined(_MSC_VER)
#define XPROC_COMPILER_MSVC 1
#endif

#if defined(XPROC_COMPILER_MSVC)
#define XPROC_FORCE_INLINE __forceinline
#define XPROC_EXPORT __declspec(dllexport)
#define XPROC_IMPORT __declspec(dllimport)
#else
#define XPROC_FORCE_INLINE inline __attribute__((always_inline))
#define XPROC_EXPORT __attribute__((visibility("default")))
#define XPROC_IMPORT
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
#define XPROC_ARCH_X86_64 1
#define XPROC_ARCH_NAME "x86_64"
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(__ARM_ARCH_8A)
#define XPROC_ARCH_ARM64 1
#define XPROC_ARCH_NAME "ARM64"
#elif defined(__i386__) || defined(_M_IX86)
#define XPROC_ARCH_X86 1
#define XPROC_ARCH_NAME "x86"
#elif defined(__arm__) || defined(_M_ARM)
#define XPROC_ARCH_ARM 1
#define XPROC_ARCH_NAME "ARM"
#else
#define XPROC_ARCH_UNKNOWN 1
#define XPROC_ARCH_NAME "Unknown"
#endif

#if defined(XPROC_ARCH_X86_64) || defined(XPROC_ARCH_X86)
#if defined(XPROC_COMPILER_MSVC)
#include <intrin.h>
#define XPROC_CPU_PAUSE() _mm_pause()
#else
#define XPROC_CPU_PAUSE() __asm__ __volatile__("pause")
#endif
#elif defined(XPROC_ARCH_ARM64) || defined(XPROC_ARCH_ARM)
#if defined(XPROC_COMPILER_MSVC)
#include <intrin.h>
#define XPROC_CPU_PAUSE() __yield()
#else
#define XPROC_CPU_PAUSE() __asm__ __volatile__("yield")
#endif
#else
#include <thread>
#define XPROC_CPU_PAUSE() std::this_thread::yield()
#endif

#ifndef XPROC_CACHE_LINE_SIZE
#define XPROC_CACHE_LINE_SIZE 64
#endif

#define XPROC_ALIGNAS_CACHE_LINE alignas(XPROC_CACHE_LINE_SIZE)

namespace xproc {
namespace platform {

struct platform_info
{
    static constexpr const char *os = XPROC_OS_NAME;

    static constexpr bool is_linux()
    {
#ifdef XPROC_PLATFORM_LINUX
        return true;
#else
        return false;
#endif
    }

    static constexpr bool is_windows()
    {
#ifdef XPROC_PLATFORM_WINDOWS
        return true;
#else
        return false;
#endif
    }
};

struct arch_info
{
    static constexpr const char *name = XPROC_ARCH_NAME;

    static constexpr bool is_x86_64()
    {
#ifdef XPROC_ARCH_X86_64
        return true;
#else
        return false;
#endif
    }

    static constexpr bool is_arm64()
    {
#ifdef XPROC_ARCH_ARM64
        return true;
#else
        return false;
#endif
    }
};

}// namespace platform
}// namespace xproc
