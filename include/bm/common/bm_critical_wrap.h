/**
 * @file bm_critical_wrap.h
 * @brief 临界区进入/退出宏封装
 *
 * 根据 BM_CONFIG_ENABLE_PRIORITY_MASK 选择全局关中断或优先级阈值屏蔽。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 *
 */
#ifndef BM_CRITICAL_WRAP_H
#define BM_CRITICAL_WRAP_H

#include "hal/bm_hal_critical.h"

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
/** 屏蔽低于 HRT 阈值的中断并进入临界区 */
#define BM_CRITICAL_ENTER() \
    bm_hal_critical_enter_below((uint8_t)BM_CONFIG_HRT_PRIORITY_THRESHOLD)
#define BM_CRITICAL_EXIT(state) bm_hal_critical_exit_below(state)
#else
/** 全局关中断进入临界区 */
#define BM_CRITICAL_ENTER() bm_hal_critical_enter()
#define BM_CRITICAL_EXIT(state) bm_hal_critical_exit(state)
#endif

#endif /* BM_CRITICAL_WRAP_H */
