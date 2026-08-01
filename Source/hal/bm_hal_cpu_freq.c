/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_cpu_freq.c
 * @brief CPU 主频开机对账（纯逻辑 + config 门面）
 *
 * 提供 bm_hal_cpu_freq_check 纯对账逻辑（不依赖 port，可独立单测）与
 * bm_hal_cpu_freq_check_config 门面（喂 config 宏 + port 查询），
 * 用于开机时校验 config 声明的主频/DVFS 点集与 port 运行期真值是否一致。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-03
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-03       1.0            zeh            正式发布
 * 2026-08-01       1.0            Codex           补全 Doxygen 合规注释
 *
 */
#include <stddef.h> /* NULL */
#include "hal/bm_hal_cpu.h"
#include "bm/common/bm_types.h"
#include "bm_config.h"

/**
 * @brief 判断 hz 是否在 points[0..n) 点集内
 * @param hz     待判定频率（Hz）
 * @param points 点集数组
 * @param n      点集元素个数
 * @return 1 命中；0 未命中
 */
static int freq_in_set(uint32_t hz, const uint32_t *points, uint32_t n) {
    for (uint32_t i = 0u; i < n; ++i) {
        if (points[i] == hz) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief 校验 config 声明的主频/频率点与 port 运行期真值一致（纯逻辑，便于单测）。
 * @param cfg_freq   BM_CONFIG_CPU_FREQ_HZ；0=未声明，直接返回 BM_OK
 * @param cfg_points config DVFS 点数组（可 NULL）
 * @param cfg_n      config 点数
 * @param port_freq  bm_hal_cpu_freq_hz()
 * @param port_points bm_hal_cpu_freq_points() 的表
 * @param port_n     port 点数
 * @return BM_OK 一致；BM_ERR_INVALID 主频不符/某 cfg 点不在 port 支持集/ref 不在 port 支持集
 */
int bm_hal_cpu_freq_check(uint32_t cfg_freq, const uint32_t *cfg_points, uint32_t cfg_n,
                          uint32_t port_freq, const uint32_t *port_points, uint32_t port_n) {
    if (cfg_freq == 0u) {
        return BM_OK; /* 未声明主频：跳过对账 */
    }
    if (cfg_freq != port_freq) {
        return BM_ERR_INVALID;
    }
    if (!freq_in_set(cfg_freq, port_points, port_n)) {
        return BM_ERR_INVALID; /* ref 须为支持频率之一 */
    }
    for (uint32_t i = 0u; i < cfg_n; ++i) {
        if (!freq_in_set(cfg_points[i], port_points, port_n)) {
            return BM_ERR_INVALID; /* cfg 点须 ⊆ port 支持集 */
        }
    }
    return BM_OK;
}

/**
 * @brief 开机对账门面：把 config 宏与 port 查询喂给 bm_hal_cpu_freq_check。
 * @return 见 bm_hal_cpu_freq_check。应用/未来 PM 可在启动时可选调用（不强制）。
 */
int bm_hal_cpu_freq_check_config(void) {
    const uint32_t *port_points = NULL;
    uint32_t port_n = 0u;
#ifdef BM_CONFIG_CPU_DVFS_POINTS_HZ
    static const uint32_t cfg_points[] = BM_CONFIG_CPU_DVFS_POINTS_HZ;
    const uint32_t cfg_n = (uint32_t)(sizeof cfg_points / sizeof cfg_points[0]);
#else
    const uint32_t *cfg_points = NULL;
    const uint32_t cfg_n = 0u;
#endif
    if (bm_hal_cpu_freq_points(&port_points, &port_n) != BM_OK) {
        return BM_ERR_INVALID;
    }
    return bm_hal_cpu_freq_check((uint32_t)BM_CONFIG_CPU_FREQ_HZ, cfg_points, cfg_n,
                                 bm_hal_cpu_freq_hz(), port_points, port_n);
}
