/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_estop_input.c
 * @brief 急停输入通用组件实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增急停输入通用组件
 */
#include "bm/component/bm_estop_input.h"
#include "bm_uptime.h"

#include <stddef.h>

static void bm_estop_input_exti_cb(uint32_t pin, void *user) {
    bm_estop_input_t *estop = (bm_estop_input_t *)user;

    (void)pin;
    if (estop == NULL) {
        return;
    }
    /* 消抖由 poll 完成；EXTI 仅唤醒，不直接触发 */
    (void)bm_uptime_us();
}

int bm_estop_input_validate_config(const bm_estop_input_config_t *config) {
    if (config == NULL || config->gpio == NULL || config->stable_us == 0u) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

int bm_estop_input_init(bm_estop_input_t *estop) {
    int rc;
    uint32_t flags;

    if (estop == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_estop_input_validate_config(&estop->config) != BM_OK) {
        return BM_ERR_INVALID;
    }

    bm_estop_input_reset(estop);

    rc = bm_hal_gpio_configure(estop->config.gpio, estop->config.pin,
                               BM_GPIO_INPUT | BM_GPIO_PULL_UP);
    if (rc != BM_OK) {
        return rc;
    }

    flags = estop->config.active_low ? BM_GPIO_EXTI_FALLING : BM_GPIO_EXTI_RISING;
    rc = bm_hal_gpio_exti_configure(estop->config.gpio, estop->config.pin,
                                    flags, bm_estop_input_exti_cb, estop);
    if (rc != BM_OK) {
        return rc;
    }
    return bm_hal_gpio_exti_enable(estop->config.gpio, estop->config.pin, 1);
}

void bm_estop_input_reset(bm_estop_input_t *estop) {
    if (estop == NULL) {
        return;
    }
    estop->state.debounce.config.stable_us = estop->config.stable_us;
    (void)bm_input_debounce_init(&estop->state.debounce);
    estop->state.active = 0;
    estop->state.latched = 0;
    estop->state.last_event_us = 0u;
    estop->state.event_count = 0u;
}

void bm_estop_input_clear_latch(bm_estop_input_t *estop) {
    if (estop == NULL) {
        return;
    }
    estop->state.latched = 0;
}

int bm_estop_input_active(const bm_estop_input_t *estop) {
    if (estop == NULL) {
        return 0;
    }
    return estop->state.active;
}

int bm_estop_input_latched(const bm_estop_input_t *estop) {
    if (estop == NULL) {
        return 0;
    }
    return estop->state.latched;
}

void bm_estop_input_poll(bm_estop_input_t *estop) {
    int level;
    int active;
    uint64_t now;

    if (estop == NULL) {
        return;
    }
    if (bm_hal_gpio_read(estop->config.gpio, estop->config.pin, &level) != BM_OK) {
        return;
    }
    now = bm_uptime_us();
    active = estop->config.active_low ? (level == 0) : (level != 0);

    if (bm_input_debounce_update(&estop->state.debounce, active, now)) {
        estop->state.active = bm_input_debounce_filtered(&estop->state.debounce);
        if (estop->state.active) {
            estop->state.latched = 1;
        }
        estop->state.last_event_us = now;
        estop->state.event_count++;
        if (estop->resources.estop_cb != NULL) {
            estop->resources.estop_cb(estop->resources.user, estop->config.pin,
                                      estop->state.active, now);
        }
    }
}
