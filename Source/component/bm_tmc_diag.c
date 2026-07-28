/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_tmc_diag.c
 * @brief TMC DIAG 通用输入组件实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 TMC DIAG 通用输入组件
 */
#include "bm/component/bm_tmc_diag.h"
#include "bm_uptime.h"

#include <stddef.h>

static void bm_tmc_diag_exti_cb(uint32_t pin, void *user) {
    bm_tmc_diag_t *diag = (bm_tmc_diag_t *)user;
    int level;
    int active;

    (void)pin;
    if (diag == NULL) {
        return;
    }
    if (bm_hal_gpio_read(diag->config.gpio, diag->config.pin, &level) != BM_OK) {
        level = 0;
    }
    active = diag->config.active_low ? (level == 0) : (level != 0);
    diag->state.active = active ? 1 : 0;
    if (active) {
        diag->state.latched = 1;
    }
    diag->state.last_event_us = bm_uptime_us();
    diag->state.event_count++;

    if (diag->resources.diag_cb != NULL) {
        diag->resources.diag_cb(diag->resources.user, diag->config.pin,
                                diag->state.last_event_us);
    }
}

int bm_tmc_diag_validate_config(const bm_tmc_diag_config_t *config) {
    if (config == NULL || config->gpio == NULL) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

int bm_tmc_diag_init(bm_tmc_diag_t *diag) {
    int rc;
    uint32_t flags;

    if (diag == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_tmc_diag_validate_config(&diag->config) != BM_OK) {
        return BM_ERR_INVALID;
    }

    bm_tmc_diag_reset(diag);

    rc = bm_hal_gpio_configure(diag->config.gpio, diag->config.pin,
                               BM_GPIO_INPUT | BM_GPIO_PULL_UP);
    if (rc != BM_OK) {
        return rc;
    }

    flags = diag->config.active_low ? BM_GPIO_EXTI_FALLING : BM_GPIO_EXTI_RISING;
    rc = bm_hal_gpio_exti_configure(diag->config.gpio, diag->config.pin,
                                    flags, bm_tmc_diag_exti_cb, diag);
    if (rc != BM_OK) {
        return rc;
    }
    return bm_hal_gpio_exti_enable(diag->config.gpio, diag->config.pin, 1);
}

void bm_tmc_diag_reset(bm_tmc_diag_t *diag) {
    if (diag == NULL) {
        return;
    }
    diag->state.active = 0;
    diag->state.latched = 0;
    diag->state.last_event_us = 0u;
    diag->state.event_count = 0u;
}

void bm_tmc_diag_clear_latch(bm_tmc_diag_t *diag) {
    if (diag == NULL) {
        return;
    }
    diag->state.latched = 0;
}

int bm_tmc_diag_active(const bm_tmc_diag_t *diag) {
    if (diag == NULL) {
        return 0;
    }
    return diag->state.active;
}

int bm_tmc_diag_latched(const bm_tmc_diag_t *diag) {
    if (diag == NULL) {
        return 0;
    }
    return diag->state.latched;
}
