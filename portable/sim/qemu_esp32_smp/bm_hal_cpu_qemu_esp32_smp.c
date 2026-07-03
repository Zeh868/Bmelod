/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_cpu_qemu_esp32_smp.c
 * @brief QEMU ESP32 Xtensa SMP CPU HAL（PRID / DPORT APP_CPU 释放）
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

#include <stdint.h>

/** DPORT 基址 */
#define DPORT_BASE                0x3FF00000UL
#define DPORT_APPCPU_CTRL_A       (*(volatile uint32_t *)(DPORT_BASE + 0x02C))
#define DPORT_APPCPU_CTRL_B       (*(volatile uint32_t *)(DPORT_BASE + 0x030))
#define DPORT_APPCPU_CTRL_D       (*(volatile uint32_t *)(DPORT_BASE + 0x038))

extern void bm_esp32_secondary_startup(void);
extern volatile uint32_t g_secondary_done[BM_CONFIG_CPU_COUNT];

/** 主核写入的从核框架入口（由 C 包装读取） */
volatile uintptr_t s_esp32_secondary_entry;

static uint32_t s_next_secondary_cpu = 1u;
static uint32_t s_secondary_booted[BM_CONFIG_CPU_COUNT];

/**
 * @brief 读 PRID 寄存器提取 PRO(0) / APP(1) 核号
 */
static uint32_t esp32_get_core_id(void) {
    uint32_t id;

    __asm__ volatile("rsr.prid %0" : "=a"(id));
    return (id >> 13) & 1u;
}

void bm_hal_cpu_init(void) {
    s_next_secondary_cpu = 1u;
}

uint32_t bm_hal_cpu_id(void) {
    return esp32_get_core_id();
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

    s_esp32_secondary_entry = entry_pc;
    s_secondary_booted[cpu] = 1u;

    /* 1. 使能 APP_CPU 时钟 */
    DPORT_APPCPU_CTRL_D |= 1u;
    /* 2. 设置 APP_CPU 复位向量（汇编入口，负责设栈） */
    DPORT_APPCPU_CTRL_A = (uint32_t)(uintptr_t)bm_esp32_secondary_startup;
    /* 3. 释放 APP_CPU 复位 */
    DPORT_APPCPU_CTRL_B |= 1u;

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
    __asm__ volatile("waiti 0" ::: "memory");
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
