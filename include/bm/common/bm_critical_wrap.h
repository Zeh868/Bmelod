/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_critical_wrap.h
 * @brief 临界区进入/退出宏封装
 *
 * 根据 BM_CONFIG_ENABLE_PRIORITY_MASK 选择全局关中断或优先级阈值屏蔽。
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-31
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-07-30       1.1            zeh            新增 HRT ISR 上下文标记原语
 *                                               （bm_hrt_isr_enter/exit、bm_in_hrt_isr），
 *                                               支撑掩码模式下的运行期 fail-closed
 * 2026-07-31       1.2            zeh            下沉 BM_SRT_QUEUE_API_FORBIDDEN()，
 * 2026-08-01       1.2            Codex           补全 Doxygen 合规注释
 *                                               统一 event/ultra/mempool 的
 *                                               fail-closed 判定口径
 *
 */
#ifndef BM_CRITICAL_WRAP_H
#define BM_CRITICAL_WRAP_H

#include "bm/common/bm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 进入临界区并保存先前 IRQ 状态
 * @return 先前 IRQ 状态
 */
extern bm_irq_state_t bm_critical_enter(void);

/**
 * @brief 退出临界区并恢复 IRQ 状态
 * @param state bm_critical_enter 返回的状态值
 */
extern void bm_critical_exit(bm_irq_state_t state);

/**
 * @brief 当前是否处于 ISR/中断上下文
 * @return 非 0 表示在 ISR 中
 */
extern int bm_in_isr(void);

/**
 * @brief 标记进入 HRT 级 ISR 上下文（按核嵌套计数）
 *
 * 框架 Scheduled HRT 路径（hrt_dispatch，覆盖定时器 ISR 与协作式 bm_hrt_poll
 * 两条驱动路径）已自动维护；Hardware HRT 端口（厂商 IRQ handler，如 DMA/SPI
 * 完成中断）若会在 HRT 优先级调用框架 API，应在 handler 入口/出口成对调用
 * 本原语。
 *
 * 掩码模式（BM_CONFIG_ENABLE_PRIORITY_MASK=1）下，event/ultra/mempool 等 SRT
 * 队列 API 用 BM_SRT_QUEUE_API_FORBIDDEN() 对 HRT 级上下文 fail-closed；
 * 非掩码模式计数照常维护但不影响行为（全关中断下 ISR 调用本就互斥安全）。
 *
 * @note 实现位于 Source/core/bm_hrt_isr_context.c；与 bm_in_isr 的 HAL 后端
 *       判定互补：bm_in_isr 回答"是否在中断里"，bm_in_hrt_isr 回答
 *       "是否在 HRT 级（>= 阈值）中断里"。
 */
extern void bm_hrt_isr_enter(void);

/**
 * @brief 标记退出 HRT 级 ISR 上下文（与 bm_hrt_isr_enter 成对）
 */
extern void bm_hrt_isr_exit(void);

/**
 * @brief 当前是否处于 HRT 级 ISR 上下文
 * @return 非 0 表示在 HRT 级 ISR 中（嵌套深度 > 0）
 */
extern int bm_in_hrt_isr(void);

#ifdef __cplusplus
}
#endif

#ifndef BM_CONFIG_ENABLE_PRIORITY_MASK
#define BM_CONFIG_ENABLE_PRIORITY_MASK 0
#endif

#ifndef BM_HAL_HAS_PRIORITY_MASK
#define BM_HAL_HAS_PRIORITY_MASK 0
#endif

#if BM_CONFIG_ENABLE_PRIORITY_MASK
#if !BM_HAL_HAS_PRIORITY_MASK
#error "BM_CONFIG_ENABLE_PRIORITY_MASK 需要 BM_HAL_HAS_PRIORITY_MASK"
#endif
#ifndef BM_CONFIG_HRT_PRIORITY_THRESHOLD
#define BM_CONFIG_HRT_PRIORITY_THRESHOLD 4
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 屏蔽优先级低于阈值的中断并进入临界区
 * @param threshold 优先级阈值（低于该值的中断被屏蔽）
 * @return 先前 IRQ 状态
 */
extern bm_irq_state_t bm_critical_enter_below(uint8_t threshold);

/**
 * @brief 退出按优先级屏蔽的临界区
 * @param previous_state bm_critical_enter_below 返回的状态值
 */
extern void bm_critical_exit_below(bm_irq_state_t previous_state);

#ifdef __cplusplus
}
#endif

/** 屏蔽低于 HRT 阈值的中断并进入临界区 */
#define BM_CRITICAL_ENTER() \
    bm_critical_enter_below((uint8_t)BM_CONFIG_HRT_PRIORITY_THRESHOLD)
#define BM_CRITICAL_EXIT(state) bm_critical_exit_below(state)

/**
 * @brief 当前上下文是否禁止调用 SRT 队列 API（event/ultra/mempool）
 *
 * 掩码模式下 BM_CRITICAL_ENTER() 仅屏蔽低于 HRT 阈值的中断，HRT 级 ISR 与
 * SRT 路径不互斥，放行会静默损坏队列索引，故各 SRT 队列 API 入口据此
 * fail-closed。判定只看上下文、不看调用了哪个变体——在 HRT 级 ISR 中调用
 * 非 from_isr 变体同样不安全，且是更常见的误用。
 */
#define BM_SRT_QUEUE_API_FORBIDDEN() (bm_in_hrt_isr() != 0)
#else
/** 全局关中断进入临界区 */
#define BM_CRITICAL_ENTER() bm_critical_enter()
#define BM_CRITICAL_EXIT(state) bm_critical_exit(state)

/** @brief 非掩码模式：全关中断下任何上下文调用 SRT 队列 API 均互斥安全 */
#define BM_SRT_QUEUE_API_FORBIDDEN() 0
#endif

#endif /* BM_CRITICAL_WRAP_H */
