/*
 * lp_platform.h for the ldn_mitm sysmodule.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <stratosphere.hpp>
#include <cstdarg>
#include <cstdio>
#include "lanplay/lp_platform.h"
#include "../../../ldn_mitm/source/debug.hpp"

namespace {

    using namespace ams;

    constexpr size_t StackSizes[LP_THREAD_COUNT] = { 0x4000, 0x3000 };
    constexpr s32    Priorities[LP_THREAD_COUNT] = { 0x14, 0x16 };
    constexpr const char *Names[LP_THREAD_COUNT] = { "lanplay::Tunnel", "lanplay::Info" };

    alignas(os::ThreadStackAlignment) constinit u8 g_stack_tunnel[0x4000];
    alignas(os::ThreadStackAlignment) constinit u8 g_stack_info[0x3000];
    u8 * const g_stacks[LP_THREAD_COUNT] = { g_stack_tunnel, g_stack_info };

    os::ThreadType g_threads[LP_THREAD_COUNT];
    constinit bool g_started[LP_THREAD_COUNT];
    struct Arg { void (*fn)(void *); void *arg; };
    Arg g_args[LP_THREAD_COUNT];

    constinit os::SdkRecursiveMutex g_lock;

    void Entry(void *p) {
        auto *a = static_cast<Arg *>(p);
        a->fn(a->arg);
    }

}

extern "C" {

int lp_thread_start(int slot, void (*fn)(void *), void *arg) {
    if (slot < 0 || slot >= LP_THREAD_COUNT || g_started[slot]) {
        return -1;
    }
    g_args[slot] = { fn, arg };
    if (R_FAILED(os::CreateThread(std::addressof(g_threads[slot]), Entry, std::addressof(g_args[slot]), g_stacks[slot], StackSizes[slot], Priorities[slot], 3))) {
        return -1;
    }
    os::SetThreadNamePointer(std::addressof(g_threads[slot]), Names[slot]);
    os::StartThread(std::addressof(g_threads[slot]));
    g_started[slot] = true;
    return 0;
}

void lp_thread_join(int slot) {
    if (slot < 0 || slot >= LP_THREAD_COUNT || !g_started[slot]) {
        return;
    }
    os::WaitThread(std::addressof(g_threads[slot]));
    os::DestroyThread(std::addressof(g_threads[slot]));
    g_started[slot] = false;
}

uint64_t lp_now_ms(void) {
    return static_cast<uint64_t>(os::GetSystemTick().ToTimeSpan().GetMilliSeconds());
}

void lp_sleep_ms(uint32_t ms) {
    os::SleepThread(TimeSpan::FromMilliSeconds(ms));
}

void lp_random(void *buf, size_t len) {
    os::GenerateRandomBytes(buf, len);
}

uint32_t lp_platform_dns_server(void) {
    u32 address, netmask, gateway, primary_dns, secondary_dns;
    if (R_SUCCEEDED(nifmGetCurrentIpConfigInfo(&address, &netmask, &gateway, &primary_dns, &secondary_dns))) {
        return __builtin_bswap32(primary_dns);
    }
    return 0;
}

void lp_lock(void)   { g_lock.Lock(); }
void lp_unlock(void) { g_lock.Unlock(); }

void lp_log(const char *fmt, ...) {
    char buf[0xC0];
    std::va_list ap;
    va_start(ap, fmt);
    util::TVSNPrintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    LogFormat("%s", buf);
}

}
