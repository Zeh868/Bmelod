/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_input_debounce.h
 * @brief 通用输入防抖的零上层依赖词汇与算法
 *
 * 提供静态分配的防抖配置、状态和纯时间滤波算法，供多个组件复用。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            下沉输入防抖共享词汇与纯算法
 */
#ifndef BM_COMMON_INPUT_DEBOUNCE_H
#define BM_COMMON_INPUT_DEBOUNCE_H

#include "bm/common/bm_types.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 输入防抖配置 */
typedef struct {
    uint32_t stable_us;
} bm_input_debounce_config_t;

/** @brief 输入防抖运行状态 */
typedef struct {
    int      raw;
    int      filtered;
    int      stable;
    uint64_t last_edge_us;
    uint64_t last_raw_us;
    uint32_t event_count;
} bm_input_debounce_state_t;

/** @brief 静态分配的输入防抖实例 */
typedef struct {
    bm_input_debounce_config_t config;
    bm_input_debounce_state_t  state;
} bm_input_debounce_t;

/** @brief 校验输入防抖配置
 * @return BM_OK 合法；BM_ERR_INVALID 参数或稳定时间非法
 */
static inline int bm_input_debounce_common_validate_config(
    const bm_input_debounce_config_t *config) {
    return (config != NULL && config->stable_us != 0u) ? BM_OK : BM_ERR_INVALID;
}

/** @brief 复位输入防抖状态
 * @param deb 输入防抖实例；NULL 时静默返回
 */
static inline void bm_input_debounce_common_reset(bm_input_debounce_t *deb) {
    if (deb == NULL) {
        return;
    }
    deb->state.raw = 0;
    deb->state.filtered = 0;
    deb->state.stable = 0;
    deb->state.last_edge_us = 0u;
    deb->state.last_raw_us = 0u;
    deb->state.event_count = 0u;
}

/** @brief 输入一份原始电平并执行时间防抖
 * @param deb 输入防抖实例；NULL 时返回 0
 * @param raw 原始电平，非零视为高电平
 * @param now_us 单调时间戳，单位微秒
 * @return 1 发生稳定边沿；0 未发生或参数为 NULL
 */
static inline int bm_input_debounce_common_update(
    bm_input_debounce_t *deb, int raw, uint64_t now_us) {
    int changed;

    if (deb == NULL) {
        return 0;
    }
    raw = raw ? 1 : 0;
    if (raw != deb->state.raw) {
        deb->state.raw = raw;
        deb->state.last_raw_us = now_us;
        deb->state.stable = 0;
        return 0;
    }
    if (deb->state.stable && deb->state.filtered == raw) {
        return 0;
    }
    if ((now_us - deb->state.last_raw_us) >= deb->config.stable_us) {
        changed = (deb->state.filtered != raw) ? 1 : 0;
        deb->state.filtered = raw;
        deb->state.stable = 1;
        if (changed) {
            deb->state.last_edge_us = now_us;
            deb->state.event_count++;
            return 1;
        }
    }
    return 0;
}

/** @brief 读取滤波后的稳定电平
 * @param deb 输入防抖实例；NULL 时返回 0
 * @return 0 或 1
 */
static inline int bm_input_debounce_common_filtered(
    const bm_input_debounce_t *deb) {
    return deb != NULL ? deb->state.filtered : 0;
}

/** @brief 查询输入是否已稳定
 * @param deb 输入防抖实例；NULL 时返回 false
 * @return true 已稳定；false 未稳定或参数为 NULL
 */
static inline bool bm_input_debounce_common_is_stable(
    const bm_input_debounce_t *deb) {
    return deb != NULL && deb->state.stable != 0;
}

#ifdef __cplusplus
}
#endif

#endif /* BM_COMMON_INPUT_DEBOUNCE_H */
