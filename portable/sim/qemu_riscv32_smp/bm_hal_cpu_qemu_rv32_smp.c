/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_cpu_qemu_rv32_smp.c
 * @brief QEMU RISC-V32 virt SMP CPU HAL（mhartid / mailbox / CLINT IPI）
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            正式发布
 * 2026-07-03       1.1            zeh            新增 CPU 主频接口 freq_hz/freq_points/freq_set 实现
 * 2026-07-28       1.2            zeh            从核 join 等待加入命名上限，超时返回 BM_ERR_TIMEOUT
 *
 */
#include "hal/bm_hal_cpu.h"
#include "bm_config.h"

#include <stddef.h> /* NULL */
#include <stdint.h>

/** QEMU virt CLINT 基址 */
#define CLINT_BASE     0x02000000UL
#define CLINT_MSIP(h)  (*(volatile uint32_t *)(CLINT_BASE + 4u * (h)))
/** @brief 从核退出确认轮询上限，超出后 join 返回 BM_ERR_TIMEOUT。 */
#define BM_QEMU_RV32_SECONDARY_JOIN_POLL_LIMIT  1000000u

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
    uint32_t attempt;

    for (cpu = 1u; cpu < BM_CONFIG_CPU_COUNT; cpu++) {
        if (!s_secondary_booted[cpu]) {
            continue;
        }
        for (attempt = 0u;
             attempt < BM_QEMU_RV32_SECONDARY_JOIN_POLL_LIMIT;
             ++attempt) {
            if (g_secondary_done[cpu] != 0u) {
                break;
            }
            __asm volatile ("nop" ::: "memory");
        }
        if (attempt == BM_QEMU_RV32_SECONDARY_JOIN_POLL_LIMIT) {
            return BM_ERR_TIMEOUT;
        }
    }
    return BM_OK;
}

void bm_hal_cpu_yield(void) {
    __asm volatile ("wfi" ::: "memory");
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
