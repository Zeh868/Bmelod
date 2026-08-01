/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_i2c_esp32_idf.h
 * @brief ESP32-WROOM-32E I2C 总线后端（bm_hal_i2c 设备实例）
 * @maturity E1
 *
 * 在既有 LL 原语（bm_vendor_i2c_esp32_idf）之上导出框架
 * `{api, config}` 设备模型实例 `bm_hal_i2c_0` / `bm_hal_i2c_1`：
 * - bm_hal_i2c_1：I2C_NUM_1，SDA=GPIO19 / SCL=GPIO18，400 kHz
 *   （M0 AS5600 与 BMI160 共线——须经端口互斥串行访问，均禁止 HRT 调用）；
 * - bm_hal_i2c_0：I2C_NUM_0，SDA=GPIO23 / SCL=GPIO5，100 kHz（M1 AS5600，
 *   内部弱上拉沿慢，降速规避硬件 timeout 窗口）。
 *
 * 端口初始化上移到本后端：首笔事务前幂等懒初始化
 * （bm_vendor_i2c_port_init 本身幂等），消费者不再各自初始化端口。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-08-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-08-01       1.0            zeh            新增（I2C 总线契约 ESP32 后端）
 * 2026-08-01       1.0            zeh            补全中文 Doxygen 合规注释
 * 2026-08-01       1.1            zeh            标注 I2C1 上 AS5600/BMI160 须串行、禁 HRT
 */
#ifndef BM_HAL_I2C_ESP32_IDF_H
#define BM_HAL_I2C_ESP32_IDF_H

#include "bm_hal_i2c.h"

#include "hal/i2c_types.h"
#include "soc/gpio_num.h"

/**
 * @brief ESP32 I2C 后端扩展配置（首成员为契约级 bm_i2c_config_t）。
 */
typedef struct bm_i2c_config_esp32 {
    bm_i2c_config_t base; /**< 契约级通用配置（时钟/忙等预算） */
    i2c_port_t      port; /**< I2C 端口号（I2C_NUM_0 / I2C_NUM_1） */
    gpio_num_t      sda;  /**< SDA GPIO */
    gpio_num_t      scl;  /**< SCL GPIO */
} bm_i2c_config_esp32_t;

/** @brief I2C0 总线实例（M1 AS5600，100 kHz）。 */
extern const bm_hal_i2c_t bm_hal_i2c_0;
/**
 * @brief I2C1 总线实例（M0 AS5600 + BMI160 共线，400 kHz）。
 *
 * 两消费方须经端口互斥串行；均禁止在 HRT 上下文调用（见 bm_hal_i2c.h）。
 */
extern const bm_hal_i2c_t bm_hal_i2c_1;

#endif /* BM_HAL_I2C_ESP32_IDF_H */
