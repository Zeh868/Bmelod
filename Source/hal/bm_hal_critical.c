/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_critical.c
 * @brief 临界区 HAL 分发层（契约 → driver API）
 *
 * 有 BM_DRV_HAS_BACKEND 时转发至 Port driver API；否则非 hard RT 下提供带
 * 编译器屏障的桩实现。
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-06-15       1.1            zeh            hard RT 禁止 fence-only 临界区桩
 * 2026-08-01       1.1            Codex           补全 Doxygen 合规注释
 *
 */
#include "bm_drv_critical.h"
#include "bm_hal_critical.h"
#include "bm_config.h"
#include "bm_types.h"
#include "bm/common/bm_atomic_ipc.h"

#if !defined(BM_DRV_HAS_BACKEND) && BM_CONFIG_HARD_RT_PROFILE
#error "hard RT profile requires a real critical-section backend"
#endif

#ifdef BM_DRV_HAS_BACKEND
extern const struct bm_critical_driver_api bm_drv_critical_api;
#define BM_CRITICAL_DRV (&bm_drv_critical_api)
#else
/*
 * 确定性流式安全：
 * Stub 在未提供真实后端时至少提供编译器屏障，防止编译器重排跨临界区
 * 的 load/store。在抢占式环境中使用 stub 仍不安全（缺乏真实 IRQ 屏蔽），
 * 端口必须提供真实后端。该屏障确保单核协作式环境下的基本正确性。
 */
/** @brief 进入桩临界区并建立完整内存栅栏 @return 桩中断状态 0 */
static bm_irq_state_t critical_stub_enter(void) {
    bm_atomic_ipc_fence_full();
    return 0;
}

/** @brief 退出桩临界区并建立完整内存栅栏 @param state 进入时保存的中断状态 */
static void critical_stub_exit(bm_irq_state_t state) {
    (void)state;
    bm_atomic_ipc_fence_full();
}

/** @brief 查询桩后端是否处于中断上下文 @return 固定返回 0 */
static int critical_stub_in_isr(void) {
    return 0;
}

#if BM_HAL_HAS_PRIORITY_MASK
/** @brief 屏蔽指定优先级阈值以下中断 @param threshold 优先级阈值 @return 桩中断状态 0 */
static bm_irq_state_t critical_stub_enter_below(uint8_t threshold) {
    (void)threshold;
    bm_atomic_ipc_fence_full();
    return 0;
}

/** @brief 恢复阈值临界区之前的中断状态 @param previous_state 之前的中断状态 */
static void critical_stub_exit_below(bm_irq_state_t previous_state) {
    (void)previous_state;
    bm_atomic_ipc_fence_full();
}
#endif

static const struct bm_critical_driver_api critical_stub = {
    critical_stub_enter,
    critical_stub_exit,
    critical_stub_in_isr,
#if BM_HAL_HAS_PRIORITY_MASK
    critical_stub_enter_below,
    critical_stub_exit_below,
#endif
};

#define BM_CRITICAL_DRV (&critical_stub)
#endif

bm_irq_state_t bm_hal_critical_enter(void) {
    if (!BM_CRITICAL_DRV->enter) {
        return 0;
    }
    return BM_CRITICAL_DRV->enter();
}

void bm_hal_critical_exit(bm_irq_state_t state) {
    if (BM_CRITICAL_DRV->exit) {
        BM_CRITICAL_DRV->exit(state);
    }
}

#if BM_HAL_HAS_PRIORITY_MASK
bm_irq_state_t bm_hal_critical_enter_below(uint8_t threshold) {
    if (!BM_CRITICAL_DRV->enter_below) {
        return 0;
    }
    return BM_CRITICAL_DRV->enter_below(threshold);
}

void bm_hal_critical_exit_below(bm_irq_state_t previous_state) {
    if (BM_CRITICAL_DRV->exit_below) {
        BM_CRITICAL_DRV->exit_below(previous_state);
    }
}
#endif

/* ISR 状态查询由所选临界区后端分派。 */
int bm_hal_in_isr(void) {
    return BM_CRITICAL_DRV->in_isr ? BM_CRITICAL_DRV->in_isr() : 0;
}

/* ---------- core 层抽象接口实现 ---------- */

bm_irq_state_t bm_critical_enter(void) {
    return bm_hal_critical_enter();
}

void bm_critical_exit(bm_irq_state_t state) {
    bm_hal_critical_exit(state);
}

int bm_in_isr(void) {
    return bm_hal_in_isr();
}

#if BM_HAL_HAS_PRIORITY_MASK
bm_irq_state_t bm_critical_enter_below(uint8_t threshold) {
    return bm_hal_critical_enter_below(threshold);
}

void bm_critical_exit_below(bm_irq_state_t previous_state) {
    bm_hal_critical_exit_below(previous_state);
}
#endif
