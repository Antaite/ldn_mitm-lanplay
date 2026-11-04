/*
 * lwIP sys_arch for an Atmosphère stratosphere sysmodule (ams::os primitives).
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <stratosphere.hpp>
#include <cstdlib>
#include <new>

extern "C" {
#include "lwip/opt.h"
#include "lwip/sys.h"
#include "lwip/err.h"
}

#include "../../../ldn_mitm/source/debug.hpp"

namespace {

    using namespace ams;

    struct Mbox {
        os::MessageQueueType mq;
        size_t count;
        uintptr_t buf[1];
    };

    constexpr size_t MaxLwipThreads = 2;
    constexpr size_t LwipStackSize  = TCPIP_THREAD_STACKSIZE;
    alignas(os::ThreadStackAlignment) constinit u8 g_stacks[MaxLwipThreads][LwipStackSize];
    os::ThreadType g_threads[MaxLwipThreads];
    constinit size_t g_thread_count = 0;

    struct ThreadArg { lwip_thread_fn fn; void *arg; };
    ThreadArg g_thread_args[MaxLwipThreads];

    constinit os::SdkRecursiveMutex g_protect;

    void ThreadEntry(void *p) {
        auto *a = static_cast<ThreadArg *>(p);
        a->fn(a->arg);
    }

    template<typename T>
    T *AllocObj(size_t extra = 0) {
        void *mem = std::malloc(sizeof(T) + extra);
        if (mem == nullptr) {
            return nullptr;
        }
        return new (mem) T;
    }

}

extern "C" {

void lp_lwip_assert(const char *msg, const char *file, int line) {
    LogFormat("lwIP assert: %s (%s:%d)", msg, file, line);
    AMS_ABORT("lwIP assertion failed");
}

u32_t lp_lwip_rand(void) {
    u32_t v;
    ams::os::GenerateRandomBytes(&v, sizeof(v));
    return v;
}

void sys_init(void) { }

u32_t sys_now(void) {
    return static_cast<u32_t>(ams::os::GetSystemTick().ToTimeSpan().GetMilliSeconds());
}

sys_prot_t sys_arch_protect(void) {
    g_protect.Lock();
    return 0;
}

void sys_arch_unprotect(sys_prot_t) {
    g_protect.Unlock();
}

/* ------------------------------------------------------------ semaphores */

err_t sys_sem_new(sys_sem_t *sem, u8_t count) {
    auto *s = AllocObj<ams::os::SemaphoreType>();
    if (s == nullptr) {
        sem->p = nullptr;
        return ERR_MEM;
    }
    ams::os::InitializeSemaphore(s, count, 0x7FFF);
    sem->p = s;
    return ERR_OK;
}

void sys_sem_free(sys_sem_t *sem) {
    if (sem->p != nullptr) {
        ams::os::FinalizeSemaphore(static_cast<ams::os::SemaphoreType *>(sem->p));
        std::free(sem->p);
        sem->p = nullptr;
    }
}

void sys_sem_signal(sys_sem_t *sem) {
    ams::os::ReleaseSemaphore(static_cast<ams::os::SemaphoreType *>(sem->p));
}

u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout) {
    auto *s = static_cast<ams::os::SemaphoreType *>(sem->p);
    const u32_t start = sys_now();
    if (timeout == 0) {
        ams::os::AcquireSemaphore(s);
    } else if (!ams::os::TimedAcquireSemaphore(s, ams::TimeSpan::FromMilliSeconds(timeout))) {
        return SYS_ARCH_TIMEOUT;
    }
    return sys_now() - start;
}

/* --------------------------------------------------------------- mutexes */

err_t sys_mutex_new(sys_mutex_t *mutex) {
    auto *m = AllocObj<ams::os::MutexType>();
    if (m == nullptr) {
        mutex->p = nullptr;
        return ERR_MEM;
    }
    ams::os::InitializeMutex(m, true, 0);
    mutex->p = m;
    return ERR_OK;
}

void sys_mutex_free(sys_mutex_t *mutex) {
    if (mutex->p != nullptr) {
        ams::os::FinalizeMutex(static_cast<ams::os::MutexType *>(mutex->p));
        std::free(mutex->p);
        mutex->p = nullptr;
    }
}

void sys_mutex_lock(sys_mutex_t *mutex) {
    ams::os::LockMutex(static_cast<ams::os::MutexType *>(mutex->p));
}

void sys_mutex_unlock(sys_mutex_t *mutex) {
    ams::os::UnlockMutex(static_cast<ams::os::MutexType *>(mutex->p));
}

/* ------------------------------------------------------------- mailboxes */

err_t sys_mbox_new(sys_mbox_t *mbox, int size) {
    if (size <= 0) {
        size = 32;
    }
    auto *b = AllocObj<Mbox>(sizeof(uintptr_t) * static_cast<size_t>(size));
    if (b == nullptr) {
        mbox->p = nullptr;
        return ERR_MEM;
    }
    b->count = static_cast<size_t>(size);
    ams::os::InitializeMessageQueue(std::addressof(b->mq), b->buf, b->count);
    mbox->p = b;
    return ERR_OK;
}

void sys_mbox_free(sys_mbox_t *mbox) {
    if (mbox->p != nullptr) {
        auto *b = static_cast<Mbox *>(mbox->p);
        ams::os::FinalizeMessageQueue(std::addressof(b->mq));
        std::free(b);
        mbox->p = nullptr;
    }
}

void sys_mbox_post(sys_mbox_t *mbox, void *msg) {
    ams::os::SendMessageQueue(std::addressof(static_cast<Mbox *>(mbox->p)->mq), reinterpret_cast<uintptr_t>(msg));
}

err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg) {
    return ams::os::TrySendMessageQueue(std::addressof(static_cast<Mbox *>(mbox->p)->mq), reinterpret_cast<uintptr_t>(msg)) ? ERR_OK : ERR_MEM;
}

err_t sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg) {
    return sys_mbox_trypost(mbox, msg);
}

u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout) {
    auto *mq = std::addressof(static_cast<Mbox *>(mbox->p)->mq);
    uintptr_t v = 0;
    const u32_t start = sys_now();
    if (timeout == 0) {
        ams::os::ReceiveMessageQueue(std::addressof(v), mq);
    } else if (!ams::os::TimedReceiveMessageQueue(std::addressof(v), mq, ams::TimeSpan::FromMilliSeconds(timeout))) {
        if (msg != nullptr) {
            *msg = nullptr;
        }
        return SYS_ARCH_TIMEOUT;
    }
    if (msg != nullptr) {
        *msg = reinterpret_cast<void *>(v);
    }
    return sys_now() - start;
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg) {
    uintptr_t v = 0;
    if (!ams::os::TryReceiveMessageQueue(std::addressof(v), std::addressof(static_cast<Mbox *>(mbox->p)->mq))) {
        return SYS_MBOX_EMPTY;
    }
    if (msg != nullptr) {
        *msg = reinterpret_cast<void *>(v);
    }
    return 0;
}

/* --------------------------------------------------------------- threads */

sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread, void *arg, int stacksize, int prio) {
    AMS_UNUSED(stacksize);
    std::scoped_lock lk(g_protect);
    if (g_thread_count >= MaxLwipThreads) {
        LogFormat("sys_thread_new: out of thread slots (%s)", name);
        AMS_ABORT("lwIP thread pool exhausted");
    }
    const size_t i = g_thread_count++;
    g_thread_args[i] = { thread, arg };
    R_ABORT_UNLESS(ams::os::CreateThread(std::addressof(g_threads[i]), ThreadEntry, std::addressof(g_thread_args[i]), g_stacks[i], LwipStackSize, prio, 3));
    ams::os::SetThreadNamePointer(std::addressof(g_threads[i]), name);
    ams::os::StartThread(std::addressof(g_threads[i]));
    return std::addressof(g_threads[i]);
}

}
