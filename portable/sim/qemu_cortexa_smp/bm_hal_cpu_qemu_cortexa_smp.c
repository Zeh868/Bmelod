/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_cpu_qemu_cortexa_smp.c
 * @brief QEMU ARMv7-A virt SMP CPU HAL（MPIDR / PSCI HVC CPU_ON）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-03
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            正式发布
 * 2026-07-03       1.1            zeh            新增 CPU 主频接口 freq_hz/freq_points/freq_set 实现
 *
 */
#include "hal/bm_hal_cpu.h"
#include "bm_config.h"

#include <stddef.h> /* NULL */
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

#ifdef BM_CONFIG_CPU_DVFS_POINTS_HZ
/** @brief DVFS 频率点表（config 声明多档主频时启用） */
static const uint32_t s_cpu_freq_points[] = BM_CONFIG_CPU_DVFS_POINTS_HZ;
#else
/** @brief 单频率点表（config 未声明 DVFS 时，退化为单点） */
static const uint32_t s_cpu_freq_points[] = { BM_CONFIG_CPU_FREQ_HZ };
#endif
/** @brief 当前主频（Hz），初值取 config 声明的标称主频 */
static uint32_t s_cpu_freq_hz = BM_CONFIG_CPU_FREQ_HZ;

uint32_t bm_hal_cpu_freq_hz(void) {
    return s_cpu_freq_hz;
}

int bm_hal_cpu_freq_points(const uint32_t **points, uint32_t *count) {
    if ((points == NULL) || (count == NULL)) {
        return BM_ERR_INVALID;
    }
    *points = s_cpu_freq_points;
    *count = (uint32_t)(sizeof s_cpu_freq_points / sizeof s_cpu_freq_points[0]);
    return BM_OK;
}

int bm_hal_cpu_freq_set(uint32_t hz) {
    /* 单核/仿真桩：校验落在支持集内后记录，令 freq_hz 反映（无真实时钟硬件） */
    for (uint32_t i = 0u; i < (sizeof s_cpu_freq_points / sizeof s_cpu_freq_points[0]); ++i) {
        if (s_cpu_freq_points[i] == hz) {
            s_cpu_freq_hz = hz;
            return BM_OK;
        }
    }
    return BM_ERR_INVALID;
}
