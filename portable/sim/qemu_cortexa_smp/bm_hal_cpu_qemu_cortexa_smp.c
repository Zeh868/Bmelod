/**
 * @file bm_hal_cpu_qemu_cortexa_smp.c
 * @brief QEMU ARMv7-A virt SMP CPU HAL（MPIDR / PSCI HVC CPU_ON）
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

/** PSCI CPU_ON（HVC 32-bit） */
#define BM_CORTEXA_PSCI_CPU_ON_32  0x84000003u

extern void bm_cortexa_boot_init_vectors(void);
extern void bm_cortexa_secondary_startup(void);

static volatile uintptr_t s_secondary_entry;
static uint32_t s_next_target_cpu = 1u;

/**
 * @brief 通过 HVC 调用 PSCI CPU_ON
 */
static int bm_cortexa_psci_cpu_on(uint32_t target_cpu, uintptr_t entry) {
    register uint32_t r0 __asm("r0") = BM_CORTEXA_PSCI_CPU_ON_32;
    register uint32_t r1 __asm("r1") = target_cpu;
    register uint32_t r2 __asm("r2") = (uint32_t)entry;
    register uint32_t r3 __asm("r3") = 0u;

    __asm volatile("hvc #0"
                   : "+r"(r0)
                   : "r"(r1), "r"(r2), "r"(r3)
                   : "memory");
    return (r0 == 0u) ? BM_OK : BM_ERR_INVALID;
}

void bm_hal_cpu_init(void) {
    if (bm_hal_cpu_is_bootstrap()) {
        bm_cortexa_boot_init_vectors();
    }
    s_next_target_cpu = 1u;
}

uint32_t bm_hal_cpu_id(void) {
    uint32_t mpidr;

    __asm volatile("mrc p15, 0, %0, c0, c0, 5" : "=r"(mpidr));
    return mpidr & 0x3u;
}

int bm_hal_cpu_is_bootstrap(void) {
    return bm_hal_cpu_id() == 0u ? 1 : 0;
}

int bm_hal_cpu_boot_secondary(uintptr_t entry_pc) {
    uint32_t target_cpu;
    int rc;

    if (!entry_pc) {
        return BM_ERR_INVALID;
    }
    if (s_next_target_cpu >= BM_CONFIG_CPU_COUNT) {
        return BM_ERR_NO_MEM;
    }
    s_secondary_entry = entry_pc;
    target_cpu = s_next_target_cpu;
    rc = bm_cortexa_psci_cpu_on(
        target_cpu, (uintptr_t)bm_cortexa_secondary_startup);
    if (rc == BM_OK) {
        s_next_target_cpu++;
    }
    return rc;
}

int bm_hal_cpu_join_secondary(void) {
    return BM_OK;
}

void bm_hal_cpu_yield(void) {
    __asm volatile("wfe" ::: "memory");
}

/**
 * @brief PSCI 从核入口 C 包装（由启动汇编调用）
 */
void bm_cortexa_secondary_entry_c(void) {
    uintptr_t entry = s_secondary_entry;

    if (entry) {
        ((void (*)(void))entry)();
    }
    for (;;) {
        __asm volatile("wfi");
    }
}
