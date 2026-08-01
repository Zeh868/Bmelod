/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_sim_native_internal.h
 * @brief bm_sim_native_internal.h 端口后端声明
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-08-01
 *
 * @par 修改日志:
 * 2026-08-01       1.0            Codex            补全中文 Doxygen 合规注释
 */
#ifndef BM_SIM_NATIVE_INTERNAL_H
#define BM_SIM_NATIVE_INTERNAL_H

#include "bm_config.h"
#include "bm_types.h"

#include <stdint.h>

extern volatile uint8_t g_sim_native_isr_depth[];

#if BM_NATIVE_SIM_CPU_LOCAL_CRITICAL
extern volatile bm_irq_state_t g_sim_native_irq_state[];
extern volatile uint8_t g_sim_native_irq_pending[];
#endif

/**
 * @brief 立即触发当前逻辑 CPU 绑定的定时器回调。
 * @param cpu 逻辑 CPU 索引。
 */
void bm_sim_native_timer_fire_callback(uint32_t cpu);

#endif /* BM_SIM_NATIVE_INTERNAL_H */
