/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_gpio_native.c
 * @brief native_sim GPIO 后端（含 EXTI 模拟）
 * @maturity E1
 *
 * 用数组维护每个 pin 的电平与 EXTI 状态，供 native 单元测试模拟输入中断。
 * 测试可通过 bm_hal_gpio_native_set_pin() 改变电平，并通过
 * bm_hal_gpio_native_fire_exti() 手动触发 EXTI 回调。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 native_sim GPIO EXTI 后端
 * 2026-07-28       1.1            zeh            fire_exti 触发时置 exti_pending，
 *                                             clear_pending 不再空操作
 * 2026-08-01       1.1            zeh            补全中文 Doxygen 合规注释
 */
#include "bm_drv_gpio.h"
#include "hal/bm_hal_gpio.h"
#include "bm/common/bm_types.h"

#include <stdint.h>
#include <string.h>

/** @brief 最大 pin 编码（含端口 A..F，每端口 16 脚）。 */
#define BM_NATIVE_GPIO_PIN_MAX 128u

/** @brief 单个 pin 状态。 */
typedef struct {
    int configured;
    uint32_t flags;
    int value;
    uint32_t exti_flags;
    bm_gpio_exti_callback_t exti_cb;
    void *exti_user;
    int exti_enabled;
    int exti_pending;
} bm_native_gpio_pin_t;

static bm_native_gpio_pin_t s_pins[BM_NATIVE_GPIO_PIN_MAX];

/**
 * @brief 重置所有 pin 状态（测试用）。
 */
void bm_hal_gpio_native_reset(void) {
    (void)memset(s_pins, 0, sizeof(s_pins));
}

/**
 * @brief 设置 pin 电平（测试用）。
 */
void bm_hal_gpio_native_set_pin(uint32_t pin, int value) {
    if (pin < BM_NATIVE_GPIO_PIN_MAX) {
        s_pins[pin].value = value ? 1 : 0;
    }
}

/**
 * @brief 手动触发 EXTI 回调（测试用）。
 *
 * 触发即置 exti_pending（对齐硬件边沿置位语义），
 * 由 bm_hal_gpio_exti_clear_pending 清除。
 */
void bm_hal_gpio_native_fire_exti(uint32_t pin) {
    bm_native_gpio_pin_t *p;

    if (pin >= BM_NATIVE_GPIO_PIN_MAX) {
        return;
    }
    p = &s_pins[pin];
    p->exti_pending = 1;
    if (p->exti_cb != NULL && p->exti_enabled) {
        p->exti_cb(pin, p->exti_user);
    }
}

/* -------------------------------------------------------------------------- */
/*  driver API 实现                                                             */
/* -------------------------------------------------------------------------- */

static int native_gpio_configure(const struct bm_hal_gpio *dev,
                                 uint32_t pin, uint32_t flags) {
    (void)dev;
    if (pin >= BM_NATIVE_GPIO_PIN_MAX) {
        return BM_ERR_INVALID;
    }
    s_pins[pin].configured = 1;
    s_pins[pin].flags = flags;
    return BM_OK;
}

/**
 * @brief 写入 GPIO 仿真引脚电平。
 * @param dev GPIO 设备实例；当前实现不使用该参数。
 * @param pin GPIO 引脚索引。
 * @param value 待写入的数值。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int native_gpio_write(const struct bm_hal_gpio *dev,
                             uint32_t pin, int value) {
    (void)dev;
    if (pin >= BM_NATIVE_GPIO_PIN_MAX) {
        return BM_ERR_INVALID;
    }
    if ((s_pins[pin].flags & BM_GPIO_OUTPUT) == 0u) {
        return BM_ERR_INVALID;
    }
    s_pins[pin].value = value ? 1 : 0;
    return BM_OK;
}

/**
 * @brief 读取 GPIO 仿真引脚电平。
 * @param dev GPIO 设备实例；当前实现不使用该参数。
 * @param pin GPIO 引脚索引。
 * @param value 用于接收读取值的输出指针；不得为 NULL。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int native_gpio_read(const struct bm_hal_gpio *dev,
                            uint32_t pin, int *value) {
    (void)dev;
    if (value == NULL) {
        return BM_ERR_INVALID;
    }
    if (pin >= BM_NATIVE_GPIO_PIN_MAX) {
        return BM_ERR_INVALID;
    }
    *value = s_pins[pin].value;
    return BM_OK;
}

/**
 * @brief 翻转 GPIO 仿真引脚电平。
 * @param dev GPIO 设备实例；当前实现不使用该参数。
 * @param pin GPIO 引脚索引。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int native_gpio_toggle(const struct bm_hal_gpio *dev, uint32_t pin) {
    (void)dev;
    if (pin >= BM_NATIVE_GPIO_PIN_MAX) {
        return BM_ERR_INVALID;
    }
    if ((s_pins[pin].flags & BM_GPIO_OUTPUT) == 0u) {
        return BM_ERR_INVALID;
    }
    s_pins[pin].value = !s_pins[pin].value;
    return BM_OK;
}

/**
 * @brief 配置 GPIO 仿真外部中断触发方式与回调。
 * @param dev GPIO 设备实例；当前实现不使用该参数。
 * @param pin GPIO 引脚索引。
 * @param flags GPIO 外部中断触发配置标志。
 * @param cb 事件回调；传入 NULL 时解除绑定。
 * @param user 回调用户上下文，调用回调时原样传入。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int native_gpio_exti_configure(const struct bm_hal_gpio *dev,
                                      uint32_t pin, uint32_t flags,
                                      bm_gpio_exti_callback_t cb,
                                      void *user) {
    (void)dev;
    if (pin >= BM_NATIVE_GPIO_PIN_MAX) {
        return BM_ERR_INVALID;
    }
    s_pins[pin].exti_flags = flags & BM_GPIO_EXTI_BOTH;
    s_pins[pin].exti_cb = cb;
    s_pins[pin].exti_user = user;
    s_pins[pin].exti_enabled = (cb != NULL) ? 1 : 0;
    s_pins[pin].exti_pending = 0;
    return BM_OK;
}

/**
 * @brief 启用或禁用 GPIO 仿真外部中断。
 * @param dev GPIO 设备实例；当前实现不使用该参数。
 * @param pin GPIO 引脚索引。
 * @param enable 非 0 表示启用，0 表示禁用。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int native_gpio_exti_enable(const struct bm_hal_gpio *dev,
                                   uint32_t pin, int enable) {
    (void)dev;
    if (pin >= BM_NATIVE_GPIO_PIN_MAX) {
        return BM_ERR_INVALID;
    }
    s_pins[pin].exti_enabled = enable ? 1 : 0;
    return BM_OK;
}

/**
 * @brief 清除 GPIO 仿真外部中断挂起标志。
 * @param dev GPIO 设备实例；当前实现不使用该参数。
 * @param pin GPIO 引脚索引。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int native_gpio_exti_clear_pending(const struct bm_hal_gpio *dev,
                                          uint32_t pin) {
    (void)dev;
    if (pin >= BM_NATIVE_GPIO_PIN_MAX) {
        return BM_ERR_INVALID;
    }
    s_pins[pin].exti_pending = 0;
    return BM_OK;
}

static const struct bm_gpio_driver_api g_native_gpio_api = {
    native_gpio_configure,
    native_gpio_write,
    native_gpio_read,
    native_gpio_toggle,
    native_gpio_exti_configure,
    native_gpio_exti_enable,
    native_gpio_exti_clear_pending,
};

/** @brief native_sim GPIO 设备（整芯片一个实例）。 */
const bm_hal_gpio_t bm_native_gpio = { &g_native_gpio_api, NULL };

/** @brief 默认控制台 GPIO 设备别名（可选）。 */
const bm_hal_gpio_t *bm_hal_gpio_native_default(void) {
    return &bm_native_gpio;
}
