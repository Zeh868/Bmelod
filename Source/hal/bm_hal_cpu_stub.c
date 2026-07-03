/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_cpu_stub.c
 * @brief CPU 抽象默认单核桩
 *
 * 无平台后端时 `bm_hal_cpu_id()` 恒为 0，与 `BM_CONFIG_CPU_COUNT==1` 等价。
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-03
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-07-03       1.1            zeh            新增 CPU 主频接口 freq_hz/freq_points/freq_set 桩实现
 *
 */
#include "hal/bm_hal_cpu.h"
#include "bm_config.h"

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

int bm_hal_cpu_join_secondary(void) {
    return BM_OK;
}

void bm_hal_cpu_yield(void) {
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
