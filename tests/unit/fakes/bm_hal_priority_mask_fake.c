/**
 * @file bm_hal_priority_mask_fake.c
 * @brief 优先级掩码 HAL 桩实现：模拟 bm_hal_critical_enter_below/exit_below
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 */

#define BM_HAL_HAS_PRIORITY_MASK 1

#include "bm_hal_critical.h"

static uint8_t g_basepri;
static uint8_t g_primask;

bm_irq_state_t bm_hal_critical_enter(void) {
    bm_irq_state_t previous = g_primask;
    g_primask = 1u;
    return previous;
}

void bm_hal_critical_exit(bm_irq_state_t state) {
    g_primask = (uint8_t)state;
}

bm_irq_state_t bm_hal_critical_enter_below(uint8_t threshold) {
    bm_irq_state_t packed = g_basepri;
    packed |= ((bm_irq_state_t)g_primask << 8);
    g_basepri = threshold;
    return packed;
}

void bm_hal_critical_exit_below(bm_irq_state_t previous_state) {
    g_basepri = (uint8_t)(previous_state & 0xFFu);
    g_primask = (uint8_t)(previous_state >> 8);
}

uint8_t bm_hal_priority_mask_fake_basepri(void) {
    return g_basepri;
}

void bm_hal_priority_mask_fake_reset(void) {
    g_basepri = 0u;
    g_primask = 0u;
}
