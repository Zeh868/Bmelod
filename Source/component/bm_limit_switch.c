/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_limit_switch.c
 * @brief 限位开关通用输入组件实现
 *
 * EXTI 回调记录原始事件与时间戳；业务周期性调用 bm_limit_switch_poll()
 * 完成消抖并触发事件回调。组件只上报事件，不决定整机动作。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增限位开关通用输入组件
 */
#include "bm/component/bm_limit_switch.h"
#include "bm_uptime.h"

#include <stddef.h>

/** @brief 未消抖时默认稳定时间（仅用于占位，实际以 config.stable_us 为准）。 */
#define BM_LIMIT_SWITCH_DEFAULT_STABLE_US 1000u

/**
 * @brief EXTI 统一入口
 */
static void bm_limit_switch_exti_cb(uint32_t pin, void *user) {
    bm_limit_switch_t *ls = (bm_limit_switch_t *)user;
    int level;

    (void)pin;
    if (ls == NULL) {
        return;
    }
    if (bm_hal_gpio_read(ls->config.gpio, ls->config.pin, &level) != BM_OK) {
        level = 0;
    }
    ls->state.triggered = level ? 1 : 0;
    if (level) {
        ls->state.latched = 1;
    }
    ls->state.last_event_us = bm_uptime_us();
    ls->state.event_count++;

    /* 无消抖时直接回调 */
    if (ls->config.stable_us == 0u && ls->resources.event_cb != NULL) {
        ls->resources.event_cb(ls->resources.user, ls->config.pin,
                               ls->state.triggered, ls->state.last_event_us);
    }
}

int bm_limit_switch_validate_config(const bm_limit_switch_config_t *config) {
    if (config == NULL || config->gpio == NULL) {
        return BM_ERR_INVALID;
    }
    if ((config->flags & BM_GPIO_EXTI_BOTH) == 0u) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

int bm_limit_switch_init(bm_limit_switch_t *ls) {
    int rc;

    if (ls == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_limit_switch_validate_config(&ls->config) != BM_OK) {
        return BM_ERR_INVALID;
    }

    bm_limit_switch_reset(ls);

    rc = bm_hal_gpio_exti_configure(ls->config.gpio, ls->config.pin,
                                    ls->config.flags,
                                    bm_limit_switch_exti_cb, ls);
    if (rc != BM_OK) {
        return rc;
    }
    return bm_hal_gpio_exti_enable(ls->config.gpio, ls->config.pin, 1);
}

void bm_limit_switch_reset(bm_limit_switch_t *ls) {
    if (ls == NULL) {
        return;
    }
    if (ls->config.stable_us != 0u) {
        ls->state.debounce.config.stable_us = ls->config.stable_us;
        (void)bm_input_debounce_init(&ls->state.debounce);
    }
    ls->state.triggered = 0;
    ls->state.latched = 0;
    ls->state.last_event_us = 0u;
    ls->state.event_count = 0u;
}

void bm_limit_switch_clear_latch(bm_limit_switch_t *ls) {
    if (ls == NULL) {
        return;
    }
    ls->state.latched = 0;
}

int bm_limit_switch_triggered(const bm_limit_switch_t *ls) {
    if (ls == NULL) {
        return 0;
    }
    return ls->state.triggered;
}

int bm_limit_switch_latched(const bm_limit_switch_t *ls) {
    if (ls == NULL) {
        return 0;
    }
    return ls->state.latched;
}

/**
 * @brief 周期轮询：完成消抖并触发稳定事件回调
 *
 * 业务在控制循环或 ticker 中周期性调用；NULL 时静默返回。
 *
 * @param ls 实例指针
 */
void bm_limit_switch_poll(bm_limit_switch_t *ls) {
    int level;
    uint64_t now;

    if (ls == NULL || ls->config.stable_us == 0u) {
        return;
    }
    if (bm_hal_gpio_read(ls->config.gpio, ls->config.pin, &level) != BM_OK) {
        return;
    }
    now = bm_uptime_us();
    if (bm_input_debounce_update(&ls->state.debounce, level, now)) {
        ls->state.triggered = bm_input_debounce_filtered(&ls->state.debounce);
        if (ls->state.triggered) {
            ls->state.latched = 1;
        }
        ls->state.last_event_us = now;
        if (ls->resources.event_cb != NULL) {
            ls->resources.event_cb(ls->resources.user, ls->config.pin,
                                   ls->state.triggered, now);
        }
    }
}
