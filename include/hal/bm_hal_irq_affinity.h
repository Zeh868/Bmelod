/**
 * @file bm_hal_irq_affinity.h
 * @brief HAL IRQ 三阶段亲和 API
 *
 * 为 RT/SRT 双域提供 IRQ 配置、CPU 亲和绑定与使能的抽象接口。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
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

int bm_hal_irq_configure(int irqn, const bm_hal_irq_binding_t *binding);
int bm_hal_irq_set_affinity(int irqn, uint32_t cpu);
int bm_hal_irq_enable(int irqn);

int bm_hal_timer_set_affinity(uint32_t cpu);

#endif /* BM_HAL_IRQ_AFFINITY_H */
