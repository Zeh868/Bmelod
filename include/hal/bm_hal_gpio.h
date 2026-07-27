/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_gpio.h
 * @brief GPIO HAL 接口
 *
 * 引脚配置、读写与翻转；具体硬件由平台实现绑定（整个芯片一个设备）。
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
#ifndef BM_HAL_GPIO_H
#define BM_HAL_GPIO_H

#include "drv/bm_drv_gpio.h"
#include "bm/common/bm_types.h"

typedef struct bm_hal_gpio bm_hal_gpio_t;

/**
 * @brief 配置引脚（方向/开漏/上下拉/模拟，flags 见 bm_drv_gpio.h）
 * @param gpio  GPIO 设备实例
 * @param pin   pin 编码 (port<<4)|num
 * @param flags BM_GPIO_* 组合
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；否则为平台错误码
 */
int bm_hal_gpio_configure(const bm_hal_gpio_t *gpio, uint32_t pin, uint32_t flags);

/**
 * @brief 写引脚电平
 * @param gpio  GPIO 设备实例
 * @param pin   pin 编码
 * @param value 非零高电平，0 低电平
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；否则为平台错误码
 */
int bm_hal_gpio_write(const bm_hal_gpio_t *gpio, uint32_t pin, int value);

/**
 * @brief 读引脚电平
 * @param gpio  GPIO 设备实例
 * @param pin   pin 编码
 * @param value 输出电平（0/1）
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；BM_ERR_INVALID 参数无效
 */
int bm_hal_gpio_read(const bm_hal_gpio_t *gpio, uint32_t pin, int *value);

/**
 * @brief 翻转引脚电平
 * @param gpio GPIO 设备实例
 * @param pin  pin 编码
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；否则为平台错误码
 */
int bm_hal_gpio_toggle(const bm_hal_gpio_t *gpio, uint32_t pin);

#endif /* BM_HAL_GPIO_H */
