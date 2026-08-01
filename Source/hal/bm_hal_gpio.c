/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_gpio.c
 * @brief GPIO HAL 分发层（契约 → driver API）
 *
 * 未绑定后端时返回 BM_ERR_NOT_INIT（对齐既有分发层模式）。
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1）
 * 2026-07-28       1.1            zeh            扩展 EXTI 配置/使能/pending 清除分发
 * 2026-08-01       1.1            zeh           补全 Doxygen 合规注释
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

int bm_hal_gpio_exti_configure(const bm_hal_gpio_t *gpio, uint32_t pin,
                               uint32_t flags,
                               bm_gpio_exti_callback_t cb, void *user) {
    (void)pin;
    (void)flags;
    (void)cb;
    (void)user;
    if (!gpio || !gpio->api || !gpio->api->exti_configure) {
        return BM_ERR_NOT_INIT;
    }
    return gpio->api->exti_configure(gpio, pin, flags, cb, user);
}

int bm_hal_gpio_exti_enable(const bm_hal_gpio_t *gpio, uint32_t pin,
                            int enable) {
    (void)pin;
    (void)enable;
    if (!gpio || !gpio->api || !gpio->api->exti_enable) {
        return BM_ERR_NOT_INIT;
    }
    return gpio->api->exti_enable(gpio, pin, enable);
}

int bm_hal_gpio_exti_clear_pending(const bm_hal_gpio_t *gpio, uint32_t pin) {
    (void)pin;
    if (!gpio || !gpio->api || !gpio->api->exti_clear_pending) {
        return BM_ERR_NOT_INIT;
    }
    return gpio->api->exti_clear_pending(gpio, pin);
}
