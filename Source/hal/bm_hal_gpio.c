/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_gpio.c
 * @brief GPIO HAL 分发层（契约 → driver API）
 *
 * 未绑定后端时返回 BM_ERR_NOT_INIT（对齐既有分发层模式）。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1）
 *
 */
#include "bm_hal_gpio.h"
#include "bm_types.h"

int bm_hal_gpio_configure(const bm_hal_gpio_t *gpio, uint32_t pin, uint32_t flags) {
    if (!gpio || !gpio->api || !gpio->api->configure) {
        return BM_ERR_NOT_INIT;
    }
    return gpio->api->configure(gpio, pin, flags);
}

int bm_hal_gpio_write(const bm_hal_gpio_t *gpio, uint32_t pin, int value) {
    if (!gpio || !gpio->api || !gpio->api->write) {
        return BM_ERR_NOT_INIT;
    }
    return gpio->api->write(gpio, pin, value);
}

int bm_hal_gpio_read(const bm_hal_gpio_t *gpio, uint32_t pin, int *value) {
    if (!gpio || !gpio->api || !gpio->api->read) {
        return BM_ERR_NOT_INIT;
    }
    if (!value) {
        return BM_ERR_INVALID;
    }
    return gpio->api->read(gpio, pin, value);
}

int bm_hal_gpio_toggle(const bm_hal_gpio_t *gpio, uint32_t pin) {
    if (!gpio || !gpio->api || !gpio->api->toggle) {
        return BM_ERR_NOT_INIT;
    }
    return gpio->api->toggle(gpio, pin);
}
