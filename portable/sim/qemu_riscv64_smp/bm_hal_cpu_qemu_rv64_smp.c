/**
 * @file bm_hal_cpu_qemu_rv64_smp.c
 * @brief QEMU RISC-V64 virt SMP CPU HAL（mhartid / mailbox / CLINT IPI）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            正式发布
 *
 */
#include "hal/bm_hal_cpu.h"
#include "bm_config.h"

#include <stdint.h>

/** QEMU virt CLINT 基址 */
#define CLINT_BASE     0x02000000UL
#define CLINT_MSIP(h)  (*(volatile uint32_t *)(CLINT_BASE + 4u * (h)))

extern volatile uintptr_t g_secondary_mailbox[BM_CONFIG_CPU_COUNT];
extern volatile uint32_t g_secondary_done[BM_CONFIG_CPU_COUNT];

static uint32_t s_next_secondary_cpu = 1u;
static uint32_t s_secondary_booted[BM_CONFIG_CPU_COUNT];

void bm_hal_cpu_init(void) {
    s_next_secondary_cpu = 1u;
}

uint32_t bm_hal_cpu_id(void) {
    uint32_t id;

    __asm volatile ("csrr %0, mhartid" : "=r"(id));
    return id;
}

int bm_hal_cpu_is_bootstrap(void) {
    return bm_hal_cpu_id() == 0u ? 1 : 0;
}

int bm_hal_cpu_boot_secondary(uintptr_t entry_pc) {
    uint32_t cpu;

    if (!entry_pc) {
        return BM_ERR_INVALID;
    }
    cpu = s_next_secondary_cpu;
    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return BM_ERR_NO_MEM;
    }
    g_secondary_mailbox[cpu] = entry_pc;
    s_secondary_booted[cpu] = 1u;
    __asm volatile ("fence rw, rw" ::: "memory");
    CLINT_MSIP(cpu) = 1u;
    s_next_secondary_cpu++;
    return BM_OK;
}

int bm_hal_cpu_join_secondary(void) {
    uint32_t cpu;

    for (cpu = 1u; cpu < BM_CONFIG_CPU_COUNT; cpu++) {
        if (!s_secondary_booted[cpu]) {
            continue;
        }
        while (g_secondary_done[cpu] == 0u) {
            bm_hal_cpu_yield();
        }
    }
    return BM_OK;
}

void bm_hal_cpu_yield(void) {
    __asm volatile ("wfi" ::: "memory");
}
