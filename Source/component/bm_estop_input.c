/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_estop_input.c
 * @brief 急停输入通用组件实现
 *
 * poll-only 组件：不注册 EXTI，业务周期性调用 bm_estop_input_poll()
 * 完成消抖并触发事件回调。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增急停输入通用组件
 * 2026-07-28       1.1            zeh            移除空操作 EXTI 注册
 *                                               （poll-only，不再占用 EXTI 线）；
 *                                               reset 仅在 stable_us>0 时初始化
 *                                               消抖实例，避免吞掉 BM_ERR_INVALID
 * 2026-07-28       1.2            zeh            改用 bm/common 防抖纯算法
 */
#include "bm/component/bm_estop_input.h"
#include "bm_uptime.h"

#include <stddef.h>

int bm_estop_input_validate_config(const bm_estop_input_config_t *config) {
    if (config == NULL || config->gpio == NULL || config->stable_us == 0u) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

int bm_estop_input_init(bm_estop_input_t *estop) {
    if (estop == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_estop_input_validate_config(&estop->config) != BM_OK) {
        return BM_ERR_INVALID;
    }

    bm_estop_input_reset(estop);

    /* poll-only：仅配置 GPIO 输入，不占用 EXTI 线 */
    return bm_hal_gpio_configure(estop->config.gpio, estop->config.pin,
                                 BM_GPIO_INPUT | BM_GPIO_PULL_UP);
}

void bm_estop_input_reset(bm_estop_input_t *estop) {
    if (estop == NULL) {
        return;
    }
    if (estop->config.stable_us != 0u) {
        estop->state.debounce.config.stable_us = estop->config.stable_us;
        bm_input_debounce_common_reset(&estop->state.debounce);
    }
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

    if (bm_input_debounce_common_update(&estop->state.debounce, active, now)) {
        estop->state.active =
            bm_input_debounce_common_filtered(&estop->state.debounce);
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
