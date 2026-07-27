/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_gpio.h
 * @brief GPIO 设备驱动 API（整个芯片一个设备，pin 编码 (port<<4)|num）
 *
 * 最小 flags 集；中断绑定与 AF 复用配置刻意不做（AF 属 vendor 内部
 * 布线知识，IRQ 登记为已知缺口，限位开关等 E1 走轮询）。
 *
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
#ifndef BM_DRV_GPIO_H
#define BM_DRV_GPIO_H

#include "drv/bm_drv.h"
#include "bm/common/bm_types.h"

#include <stdint.h>

struct bm_hal_gpio;

/** @brief pin 编码：高 4 位端口序号（0=A,1=B,...），低 4 位引脚号。 */
#define BM_GPIO_PIN_ENCODE(port, num)  ((((uint32_t)(port)) << 4) | ((uint32_t)(num)))
/** @brief pin 解码：端口序号。 */
#define BM_GPIO_PIN_PORT(pin)          (((uint32_t)(pin)) >> 4)
/** @brief pin 解码：引脚号。 */
#define BM_GPIO_PIN_NUM(pin)           (((uint32_t)(pin)) & 0xFu)

/** @brief flags：输入。 */
#define BM_GPIO_INPUT       (1u << 0)
/** @brief flags：推挽输出。 */
#define BM_GPIO_OUTPUT      (1u << 1)
/** @brief flags：开漏输出（与 BM_GPIO_OUTPUT 组合）。 */
#define BM_GPIO_OUTPUT_OD   ((1u << 2) | BM_GPIO_OUTPUT)
/** @brief flags：上拉。 */
#define BM_GPIO_PULL_UP     (1u << 3)
/** @brief flags：下拉。 */
#define BM_GPIO_PULL_DOWN   (1u << 4)
/** @brief flags：模拟（施密特/上下拉关闭）。 */
#define BM_GPIO_ANALOG      (1u << 5)

struct bm_gpio_driver_api {
    int (*configure)(const struct bm_hal_gpio *dev, uint32_t pin, uint32_t flags);
    int (*write)(const struct bm_hal_gpio *dev, uint32_t pin, int value);
    int (*read)(const struct bm_hal_gpio *dev, uint32_t pin, int *value);
    int (*toggle)(const struct bm_hal_gpio *dev, uint32_t pin);
};

struct bm_hal_gpio {
    const struct bm_gpio_driver_api *api;
    const void                      *config;
};

#endif /* BM_DRV_GPIO_H */
