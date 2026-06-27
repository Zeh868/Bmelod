/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_hrt.h
 * @brief HAL 与 HRT 共用的硬件事件回调绑定类型
 *
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
#ifndef BM_HAL_HRT_H
#define BM_HAL_HRT_H

/** HRT / 硬件 ISR 回调（与 bm_hrt_callback_t 同签名） */
typedef void (*bm_hrt_callback_t)(void *context);

/** PWM/ADC 等硬件事件绑定参数 */
typedef struct bm_hal_hrt_binding {
    bm_hrt_callback_t callback;
    void             *context;
} bm_hal_hrt_binding_t;

#endif /* BM_HAL_HRT_H */
