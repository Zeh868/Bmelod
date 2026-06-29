/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_cpu_mp_native.c
 * @brief native_sim 多核 CPU HAL（TLS cpu_id + 从核线程启动）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 *
 */
#include "hal/bm_hal_cpu.h"
#include "bm_config.h"
#include "bm_hal_cpu_mp_native.h"

#include <stdint.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sched.h>
#endif

#if BM_CONFIG_CPU_COUNT <= 1u

void bm_hal_cpu_init(void) {
}

uint32_t bm_hal_cpu_id(void) {
    return 0u;
}

int bm_hal_cpu_is_bootstrap(void) {
    return 1;
}

int bm_hal_cpu_boot_secondary(uintptr_t entry_pc) {
    (void)entry_pc;
    return BM_ERR_NOT_SUPPORTED;
}

int bm_hal_cpu_native_set_id(uint32_t cpu) {
    return (cpu == 0u) ? BM_OK : BM_ERR_INVALID;
}

int bm_hal_cpu_join_secondary(void) {
    return BM_OK;
}

void bm_hal_cpu_yield(void) {
#ifdef _WIN32
    Sleep(0);
#else
    sched_yield();
#endif
}

#else /* BM_CONFIG_CPU_COUNT > 1 */

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

static DWORD s_tls_cpu_index = TLS_OUT_OF_INDEXES;
static HANDLE s_secondary_thread[BM_CONFIG_CPU_COUNT - 1u];
typedef struct {
    uintptr_t entry;
    uint32_t cpu;
} bm_native_secondary_context_t;
static bm_native_secondary_context_t
    s_secondary_context[BM_CONFIG_CPU_COUNT - 1u];
static uint32_t s_next_secondary_cpu = 1u;

static DWORD WINAPI secondary_thread_main(LPVOID arg) {
    bm_native_secondary_context_t *context =
        (bm_native_secondary_context_t *)arg;

    if (s_tls_cpu_index != TLS_OUT_OF_INDEXES) {
        TlsSetValue(
            s_tls_cpu_index, (LPVOID)(uintptr_t)context->cpu);
    }
    ((void (*)(void))context->entry)();
    return 0u;
}

void bm_hal_cpu_init(void) {
    if (s_tls_cpu_index == TLS_OUT_OF_INDEXES) {
        s_tls_cpu_index = TlsAlloc();
    }
    if (s_tls_cpu_index != TLS_OUT_OF_INDEXES) {
        TlsSetValue(s_tls_cpu_index, (LPVOID)0);
    }
    s_next_secondary_cpu = 1u;
}

uint32_t bm_hal_cpu_id(void) {
    if (s_tls_cpu_index == TLS_OUT_OF_INDEXES) {
        return 0u;
    }
    return (uint32_t)(uintptr_t)TlsGetValue(s_tls_cpu_index);
}

int bm_hal_cpu_is_bootstrap(void) {
    return bm_hal_cpu_id() == 0u ? 1 : 0;
}

int bm_hal_cpu_boot_secondary(uintptr_t entry_pc) {
    uint32_t cpu;
    uint32_t index;

    if (!entry_pc) {
        return BM_ERR_INVALID;
    }
    cpu = s_next_secondary_cpu;
    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return BM_ERR_NO_MEM;
    }
    index = cpu - 1u;
    s_secondary_context[index].entry = entry_pc;
    s_secondary_context[index].cpu = cpu;
    s_secondary_thread[index] = CreateThread(
        NULL, 0, secondary_thread_main,
        &s_secondary_context[index], 0, NULL);
    if (!s_secondary_thread[index]) {
        return BM_ERR_NO_MEM;
    }
    s_next_secondary_cpu++;
    return BM_OK;
}

int bm_hal_cpu_native_set_id(uint32_t cpu) {
    if (cpu >= BM_CONFIG_CPU_COUNT || s_tls_cpu_index == TLS_OUT_OF_INDEXES) {
        return BM_ERR_INVALID;
    }
    return TlsSetValue(s_tls_cpu_index, (LPVOID)(uintptr_t)cpu)
               ? BM_OK
               : BM_ERR_INVALID;
}

int bm_hal_cpu_join_secondary(void) {
    uint32_t index;

    for (index = 0u; index < (BM_CONFIG_CPU_COUNT - 1u); index++) {
        if (s_secondary_thread[index]) {
            WaitForSingleObject(s_secondary_thread[index], INFINITE);
            CloseHandle(s_secondary_thread[index]);
            s_secondary_thread[index] = NULL;
        }
    }
    return BM_OK;
}

void bm_hal_cpu_yield(void) {
    Sleep(0);
}

#else /* pthread */

#include <pthread.h>
#include <sched.h>

static pthread_key_t s_tls_cpu_key;
static pthread_once_t s_tls_once = PTHREAD_ONCE_INIT;
static pthread_t s_secondary_thread[BM_CONFIG_CPU_COUNT - 1u];
typedef struct {
    uintptr_t entry;
    uint32_t cpu;
} bm_native_secondary_context_t;
static bm_native_secondary_context_t
    s_secondary_context[BM_CONFIG_CPU_COUNT - 1u];
static uint32_t s_next_secondary_cpu = 1u;

static void tls_cpu_key_create(void) {
    (void)pthread_key_create(&s_tls_cpu_key, NULL);
}

static void *secondary_thread_main(void *arg) {
    bm_native_secondary_context_t *context =
        (bm_native_secondary_context_t *)arg;

    (void)pthread_setspecific(
        s_tls_cpu_key, (void *)(uintptr_t)context->cpu);
    ((void (*)(void))context->entry)();
    return NULL;
}

void bm_hal_cpu_init(void) {
    (void)pthread_once(&s_tls_once, tls_cpu_key_create);
    (void)pthread_setspecific(s_tls_cpu_key, (void *)(uintptr_t)0u);
    s_next_secondary_cpu = 1u;
}

uint32_t bm_hal_cpu_id(void) {
    void *v;

    (void)pthread_once(&s_tls_once, tls_cpu_key_create);
    v = pthread_getspecific(s_tls_cpu_key);
    return (uint32_t)(uintptr_t)v;
}

int bm_hal_cpu_is_bootstrap(void) {
    return bm_hal_cpu_id() == 0u ? 1 : 0;
}

int bm_hal_cpu_boot_secondary(uintptr_t entry_pc) {
    int rc;
    uint32_t cpu;
    uint32_t index;

    if (!entry_pc) {
        return BM_ERR_INVALID;
    }
    cpu = s_next_secondary_cpu;
    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return BM_ERR_NO_MEM;
    }
    index = cpu - 1u;
    s_secondary_context[index].entry = entry_pc;
    s_secondary_context[index].cpu = cpu;
    rc = pthread_create(
        &s_secondary_thread[index], NULL, secondary_thread_main,
        &s_secondary_context[index]);
    if (rc == 0) {
        s_next_secondary_cpu++;
    }
    return (rc == 0) ? BM_OK : BM_ERR_NO_MEM;
}

int bm_hal_cpu_native_set_id(uint32_t cpu) {
    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return BM_ERR_INVALID;
    }
    (void)pthread_once(&s_tls_once, tls_cpu_key_create);
    return (pthread_setspecific(s_tls_cpu_key, (void *)(uintptr_t)cpu) == 0)
               ? BM_OK
               : BM_ERR_INVALID;
}

int bm_hal_cpu_join_secondary(void) {
    uint32_t index;

    for (index = 0u; index < (BM_CONFIG_CPU_COUNT - 1u); index++) {
        (void)pthread_join(s_secondary_thread[index], NULL);
    }
    return BM_OK;
}

void bm_hal_cpu_yield(void) {
    sched_yield();
}

#endif /* _WIN32 */
#endif /* BM_CONFIG_CPU_COUNT */
