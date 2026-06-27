/**
 * @file bm_hal_cpu_qemu_aarch64_smp.c
 * @brief QEMU AArch64 virt SMP CPU HAL（MPIDR / PSCI CPU_ON）
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

#include <stdint.h>

/** PSCI CPU_ON（SMC 64-bit） */
#define BM_AARCH64_PSCI_CPU_ON_64  0xC4000003ULL

extern void bm_aarch64_boot_init_vectors(void);
extern void bm_aarch64_secondary_startup(void);

static volatile uintptr_t s_secondary_entry;
static uint32_t s_next_target_cpu = 1u;

/**
 * @brief 通过 SMC 调用 PSCI CPU_ON
 */
static int bm_aarch64_psci_cpu_on(uint64_t target_mpidr, uintptr_t entry) {
    register uint64_t x0 __asm("x0") = BM_AARCH64_PSCI_CPU_ON_64;
    register uint64_t x1 __asm("x1") = target_mpidr;
    register uint64_t x2 __asm("x2") = (uint64_t)entry;
    register uint64_t x3 __asm("x3") = 0;

    __asm volatile("smc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3) : "memory");
    return (x0 == 0ULL) ? BM_OK : BM_ERR_INVALID;
}

void bm_hal_cpu_init(void) {
    if (bm_hal_cpu_is_bootstrap()) {
        bm_aarch64_boot_init_vectors();
    }
    s_next_target_cpu = 1u;
}

uint32_t bm_hal_cpu_id(void) {
    uint64_t mpidr;

    __asm volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    return (uint32_t)(mpidr & 0xFFu);
}

int bm_hal_cpu_is_bootstrap(void) {
    return bm_hal_cpu_id() == 0u ? 1 : 0;
}

int bm_hal_cpu_boot_secondary(uintptr_t entry_pc) {
    uint64_t target_mpidr;
    int rc;

    if (!entry_pc) {
        return BM_ERR_INVALID;
    }
    if (s_next_target_cpu >= 2u) {
        return BM_ERR_NO_MEM;
    }
    s_secondary_entry = entry_pc;
    target_mpidr = (uint64_t)s_next_target_cpu;
    rc = bm_aarch64_psci_cpu_on(
        target_mpidr, (uintptr_t)bm_aarch64_secondary_startup);
    if (rc == BM_OK) {
        s_next_target_cpu++;
    }
    return rc;
}

int bm_hal_cpu_join_secondary(void) {
    return BM_OK;
}

void bm_hal_cpu_yield(void) {
    __asm volatile("wfe");
}

/**
 * @brief PSCI 从核入口 C 包装（由启动汇编调用）
 */
void bm_aarch64_secondary_entry_c(void) {
    uintptr_t entry = s_secondary_entry;

    if (entry) {
        ((void (*)(void))entry)();
    }
    for (;;) {
        __asm volatile("wfi");
    }
}
