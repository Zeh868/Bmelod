/**
 * @file bm_hal_irq_affinity_native.c
 * @brief native_sim IRQ 亲和后端 stub
 *
 * native_sim 单核环境不区分 CPU，所有 API 直接返回成功。
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
#include "hal/bm_hal_irq_affinity.h"

int bm_hal_irq_configure(int irqn, const bm_hal_irq_binding_t *binding) {
    (void)irqn;
    (void)binding;
    return BM_OK;
}

int bm_hal_irq_set_affinity(int irqn, uint32_t cpu) {
    (void)irqn;
    (void)cpu;
    return BM_OK;
}

int bm_hal_irq_enable(int irqn) {
    (void)irqn;
    return BM_OK;
}

int bm_hal_timer_set_affinity(uint32_t cpu) {
    (void)cpu;
    return BM_OK;
}
