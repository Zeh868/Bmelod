/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_irq_affinity.h
 * @brief HAL IRQ 三阶段亲和 API
 *
 * 为 RT/SRT 双域提供 IRQ 配置、CPU 亲和绑定与使能的抽象接口。
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-08-01       1.0            Codex           补全 Doxygen 合规注释
 *
 */
#ifndef BM_HAL_IRQ_AFFINITY_H
#define BM_HAL_IRQ_AFFINITY_H

#include "bm/common/bm_types.h"

/* 不透明外设令牌；具体 HAL 端口定义其类型。 */
typedef void *bm_hal_peripheral_t;

typedef struct {
    void (*callback)(void *context);
    void *context;
} bm_hal_irq_binding_t;

/** @brief 配置 IRQ 回调绑定 @param irqn 中断号 @param binding 回调绑定 @return BM_OK 成功；负值表示平台错误 */
int bm_hal_irq_configure(int irqn, const bm_hal_irq_binding_t *binding);
/** @brief 设置 IRQ 的 CPU 亲和性 @param irqn 中断号 @param cpu 目标 CPU @return BM_OK 成功；负值表示平台错误 */
int bm_hal_irq_set_affinity(int irqn, uint32_t cpu);
/** @brief 使能指定 IRQ @param irqn 中断号 @return BM_OK 成功；负值表示平台错误 */
int bm_hal_irq_enable(int irqn);

/** @brief 设置定时器中断的 CPU 亲和性 @param cpu 目标 CPU @return BM_OK 成功；负值表示平台错误 */
int bm_hal_timer_set_affinity(uint32_t cpu);

#endif /* BM_HAL_IRQ_AFFINITY_H */
