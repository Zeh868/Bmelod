/* SPDX-License-Identifier: LGPL-3.0-or-later */
/**
 * @file bm_mask_guard_fake.c
 * @brief 掩码模式守卫测试桩：同时实现 bm_hal_critical_* 与 bm_critical_* 两层符号
 *
 * 测试目标直编 Source/core/bm_event.c 与 bm_mempool.c（掩码宏开），本桩提供
 * HAL 层与 core 抽象层全部临界区符号，使静态库中的 bm_hal_critical.o 不被
 * 拉入（符号已全部解析），避免重复定义。同时记录 enter_below 实际阈值，
 * 供断言 BM_CRITICAL_ENTER() 确实按 BM_CONFIG_HRT_PRIORITY_THRESHOLD 屏蔽。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-30
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-07-30       1.0            zeh            正式发布
 */

#include "bm_hal_critical.h"
#include "bm_critical_wrap.h"

static uint8_t g_basepri;
static uint8_t g_primask;
static uint8_t g_last_threshold;
static unsigned g_enter_below_count;

/* ---------- HAL 层 ---------- */

bm_irq_state_t bm_hal_critical_enter(void) {
    bm_irq_state_t previous = g_primask;
    g_primask = 1u;
    return previous;
}

void bm_hal_critical_exit(bm_irq_state_t state) {
    g_primask = (uint8_t)state;
}

int bm_hal_in_isr(void) {
    return 0;
}

bm_irq_state_t bm_hal_critical_enter_below(uint8_t threshold) {
    bm_irq_state_t packed = g_basepri;

    packed |= ((bm_irq_state_t)g_primask << 8);
    g_basepri = threshold;
    g_last_threshold = threshold;
    g_enter_below_count++;
    return packed;
}

void bm_hal_critical_exit_below(bm_irq_state_t previous_state) {
    g_basepri = (uint8_t)(previous_state & 0xFFu);
    g_primask = (uint8_t)((uint32_t)previous_state >> 8);
}

/* ---------- core 抽象层（镜像 bm_hal_critical.c 的透传） ---------- */

bm_irq_state_t bm_critical_enter(void) {
    return bm_hal_critical_enter();
}

void bm_critical_exit(bm_irq_state_t state) {
    bm_hal_critical_exit(state);
}

int bm_in_isr(void) {
    return bm_hal_in_isr();
}

bm_irq_state_t bm_critical_enter_below(uint8_t threshold) {
    return bm_hal_critical_enter_below(threshold);
}

void bm_critical_exit_below(bm_irq_state_t previous_state) {
    bm_hal_critical_exit_below(previous_state);
}

/* ---------- 测试访问器 ---------- */

uint8_t bm_mask_guard_fake_last_threshold(void) {
    return g_last_threshold;
}

unsigned bm_mask_guard_fake_enter_below_count(void) {
    return g_enter_below_count;
}

void bm_mask_guard_fake_reset(void) {
    g_basepri = 0u;
    g_primask = 0u;
    g_last_threshold = 0u;
    g_enter_below_count = 0u;
}
