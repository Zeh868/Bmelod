/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_atomic_ipc.h
 * @brief 可移植原子封装
 *
 * C11 工具链优先使用 `<stdatomic.h>`；C99 工具链使用
 * GCC/Clang/MSVC 编译器原子，提供统一的 acquire/release API。
 * 单核且目标无 lock-free 32 位原子时，退化为 volatile 实现。
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-16
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-06-16       1.1            zeh            单核无 lock-free 原子时 volatile 回退
 *
 */
#ifndef BM_ATOMIC_IPC_H
#define BM_ATOMIC_IPC_H

#include "bm_config.h"
#ifndef BM_CONFIG_CPU_COUNT
#define BM_CONFIG_CPU_COUNT 1u
#endif

#include "bm/common/bm_types.h"
#include <limits.h>

/*
 * 探测 C11 / GCC 原子是否 lock-free；非 lock-free 且单核时启用 volatile 回退。
 */
#if (!defined(_MSC_VER) && defined(__STDC_VERSION__) && \
     (__STDC_VERSION__ >= 201112L) && !defined(__STDC_NO_ATOMICS__))
#include <stdatomic.h>
#if ATOMIC_INT_LOCK_FREE != 2
#if (BM_CONFIG_CPU_COUNT > 1u)
#error "bm_atomic_ipc requires always lock-free 32-bit C11 atomics when BM_CONFIG_CPU_COUNT > 1"
#else
#define BM_ATOMIC_IPC_USE_VOLATILE_FALLBACK 1
#endif
#endif
#elif defined(__GNUC__) || defined(__clang__)
#if (UINT_MAX == UINT32_MAX)
#if !defined(__GCC_ATOMIC_INT_LOCK_FREE) || \
    (__GCC_ATOMIC_INT_LOCK_FREE != 2)
#if (BM_CONFIG_CPU_COUNT > 1u)
#error "bm_atomic_ipc requires lock-free 32-bit unsigned int atomics when BM_CONFIG_CPU_COUNT > 1"
#else
#define BM_ATOMIC_IPC_USE_VOLATILE_FALLBACK 1
#endif
#endif
#elif (ULONG_MAX == UINT32_MAX)
#if !defined(__GCC_ATOMIC_LONG_LOCK_FREE) || \
    (__GCC_ATOMIC_LONG_LOCK_FREE != 2)
#if (BM_CONFIG_CPU_COUNT > 1u)
#error "bm_atomic_ipc requires lock-free 32-bit unsigned long atomics when BM_CONFIG_CPU_COUNT > 1"
#else
#define BM_ATOMIC_IPC_USE_VOLATILE_FALLBACK 1
#endif
#endif
#endif
#endif

#if (!defined(_MSC_VER) && defined(__STDC_VERSION__) && \
     (__STDC_VERSION__ >= 201112L) && !defined(__STDC_NO_ATOMICS__) && \
     !defined(BM_ATOMIC_IPC_USE_VOLATILE_FALLBACK))
typedef _Atomic uint32_t bm_atomic_ipc_u32_t;
#define BM_ATOMIC_IPC_U32_INIT(v) (v)

static inline uint32_t bm_atomic_ipc_load_u32(
    const bm_atomic_ipc_u32_t *p) {
    return atomic_load_explicit(p, memory_order_acquire);
}
static inline void bm_atomic_ipc_store_u32(
    bm_atomic_ipc_u32_t *p, uint32_t v) {
    atomic_store_explicit(p, v, memory_order_release);
}
static inline uint32_t bm_atomic_ipc_inc_u32(bm_atomic_ipc_u32_t *p) {
    return atomic_fetch_add_explicit(
        p, 1u, memory_order_acq_rel) + 1u;
}
static inline uint32_t bm_atomic_ipc_dec_u32(bm_atomic_ipc_u32_t *p) {
    return atomic_fetch_sub_explicit(
        p, 1u, memory_order_acq_rel) - 1u;
}
static inline uint32_t bm_atomic_ipc_exchange_u32(bm_atomic_ipc_u32_t *p,
                                                   uint32_t v) {
    return atomic_exchange_explicit(p, v, memory_order_acq_rel);
}
static inline int bm_atomic_ipc_compare_exchange_u32(
    bm_atomic_ipc_u32_t *p, uint32_t *expected, uint32_t desired) {
    return atomic_compare_exchange_weak_explicit(
        p, expected, desired, memory_order_acq_rel, memory_order_acquire);
}
static inline void bm_atomic_ipc_fence_release(void) {
    atomic_thread_fence(memory_order_release);
}
static inline void bm_atomic_ipc_fence_acquire(void) {
    atomic_thread_fence(memory_order_acquire);
}
static inline void bm_atomic_ipc_fence_full(void) {
    atomic_thread_fence(memory_order_seq_cst);
}
#elif defined(_MSC_VER)
#include <intrin.h>
typedef volatile long bm_atomic_ipc_u32_t;
#define BM_ATOMIC_IPC_U32_INIT(v) ((long)(v))

static inline uint32_t bm_atomic_ipc_load_u32(
    const bm_atomic_ipc_u32_t *p) {
    return (uint32_t)_InterlockedCompareExchange(
        (bm_atomic_ipc_u32_t *)(uintptr_t)p, 0, 0);
}
static inline void bm_atomic_ipc_store_u32(bm_atomic_ipc_u32_t *p, uint32_t v) {
    (void)_InterlockedExchange(p, (long)v);
}
static inline uint32_t bm_atomic_ipc_inc_u32(bm_atomic_ipc_u32_t *p) {
    return (uint32_t)_InterlockedIncrement(p);
}
static inline uint32_t bm_atomic_ipc_dec_u32(bm_atomic_ipc_u32_t *p) {
    return (uint32_t)_InterlockedDecrement(p);
}
static inline uint32_t bm_atomic_ipc_exchange_u32(bm_atomic_ipc_u32_t *p,
                                                   uint32_t v) {
    return (uint32_t)_InterlockedExchange(p, (long)v);
}
static inline int bm_atomic_ipc_compare_exchange_u32(
    bm_atomic_ipc_u32_t *p, uint32_t *expected, uint32_t desired) {
    long old = _InterlockedCompareExchange(
        p, (long)desired, (long)*expected);
    if ((uint32_t)old == *expected) {
        return 1;
    }
    *expected = (uint32_t)old;
    return 0;
}
static inline void bm_atomic_ipc_fence_release(void) {
#if defined(_M_ARM) || defined(_M_ARM64)
    __dmb(0xB);  /* DMB ISHST: store-release barrier for ARM */
#else
    _ReadWriteBarrier();  /* x86 TSO: compiler barrier suffices */
#endif
}
static inline void bm_atomic_ipc_fence_acquire(void) {
#if defined(_M_ARM) || defined(_M_ARM64)
    __dmb(0xB);  /* DMB ISH: load-acquire barrier for ARM */
#else
    _ReadWriteBarrier();  /* x86 TSO: compiler barrier suffices */
#endif
}
static inline void bm_atomic_ipc_fence_full(void) {
#if defined(_M_ARM) || defined(_M_ARM64)
    __dmb(0xF);  /* DMB SY: full sequential-consistency barrier */
#elif defined(_M_IX86) || defined(_M_X64)
    _ReadWriteBarrier();
    __faststorefence();  /* x86: drain store buffer for SC */
#else
    _ReadWriteBarrier();  /* fallback: compiler-only (single-core) */
#endif
}
#elif (defined(__GNUC__) || defined(__clang__)) && \
      !defined(BM_ATOMIC_IPC_USE_VOLATILE_FALLBACK)
typedef uint32_t bm_atomic_ipc_u32_t;
#define BM_ATOMIC_IPC_U32_INIT(v) (v)
#if (UINT_MAX == UINT32_MAX)
#elif (ULONG_MAX == UINT32_MAX)
#else
#error "bm_atomic_ipc requires a native 32-bit unsigned integer type"
#endif

static inline uint32_t bm_atomic_ipc_load_u32(
    const bm_atomic_ipc_u32_t *p) {
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}
static inline void bm_atomic_ipc_store_u32(bm_atomic_ipc_u32_t *p, uint32_t v) {
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
}
static inline uint32_t bm_atomic_ipc_inc_u32(bm_atomic_ipc_u32_t *p) {
    return __atomic_add_fetch(p, 1u, __ATOMIC_ACQ_REL);
}
static inline uint32_t bm_atomic_ipc_dec_u32(bm_atomic_ipc_u32_t *p) {
    return __atomic_sub_fetch(p, 1u, __ATOMIC_ACQ_REL);
}
static inline uint32_t bm_atomic_ipc_exchange_u32(bm_atomic_ipc_u32_t *p,
                                                   uint32_t v) {
    return __atomic_exchange_n(p, v, __ATOMIC_ACQ_REL);
}
static inline int bm_atomic_ipc_compare_exchange_u32(
    bm_atomic_ipc_u32_t *p, uint32_t *expected, uint32_t desired) {
    return __atomic_compare_exchange_n(
        p, expected, desired, 1, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}
static inline void bm_atomic_ipc_fence_release(void) {
    __atomic_thread_fence(__ATOMIC_RELEASE);
}
static inline void bm_atomic_ipc_fence_acquire(void) {
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
}
static inline void bm_atomic_ipc_fence_full(void) {
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}
#elif defined(BM_ATOMIC_IPC_USE_VOLATILE_FALLBACK)
/*
 * 单核仿真/无 lock-free 硬件原子：volatile 读写 + 空栅栏。
 */
#if (BM_CONFIG_CPU_COUNT > 1u)
#error "BM_CONFIG_CPU_COUNT > 1 requires lock-free 32-bit atomic helpers"
#endif
typedef volatile uint32_t bm_atomic_ipc_u32_t;
#define BM_ATOMIC_IPC_U32_INIT(v) (v)

static inline uint32_t bm_atomic_ipc_load_u32(
    const bm_atomic_ipc_u32_t *p) {
    return *p;
}
static inline void bm_atomic_ipc_store_u32(bm_atomic_ipc_u32_t *p, uint32_t v) {
    *p = v;
}
static inline uint32_t bm_atomic_ipc_inc_u32(bm_atomic_ipc_u32_t *p) {
    return ++(*p);
}
static inline uint32_t bm_atomic_ipc_dec_u32(bm_atomic_ipc_u32_t *p) {
    return --(*p);
}
static inline uint32_t bm_atomic_ipc_exchange_u32(bm_atomic_ipc_u32_t *p,
                                                   uint32_t v) {
    uint32_t old = *p;
    *p = v;
    return old;
}
static inline int bm_atomic_ipc_compare_exchange_u32(
    bm_atomic_ipc_u32_t *p, uint32_t *expected, uint32_t desired) {
    if (*p == *expected) {
        *p = desired;
        return 1;
    }
    *expected = *p;
    return 0;
}
static inline void bm_atomic_ipc_fence_release(void) { }
static inline void bm_atomic_ipc_fence_acquire(void) { }
static inline void bm_atomic_ipc_fence_full(void) { }
#else
#error "bm_atomic_ipc: unsupported compiler"
#endif

#endif /* BM_ATOMIC_IPC_H */
