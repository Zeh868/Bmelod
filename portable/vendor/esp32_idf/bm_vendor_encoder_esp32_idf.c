/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_encoder_esp32_idf.c
 * @brief ESP32-WROOM-32E 板级 AS5600 编码器实现（I2C 总线设备消费方）
 * @maturity E1
 *
 * 两路编码器经框架 I2C 总线实例（bm_hal_i2c_esp32_idf）读取
 * AS5600 RAW ANGLE：
 * - M0：bm_hal_i2c_1（I2C_NUM_1，SDA=GPIO19，SCL=GPIO18，400 kHz，与 BMI160 共用）
 * - M1：bm_hal_i2c_0（I2C_NUM_0，SDA=GPIO23，SCL=GPIO5，100 kHz）
 *
 * 端口懒初始化已上移到总线后端（首笔事务幂等初始化），本层不再
 * 持有端口/引脚/速率配置，也不再做端口初始化兜底。
 *
 * @author zeh (china_qzh@163.com)
 * @version 2.5
 * @date 2026-08-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            新增编码器实现
 * 2026-06-19       1.1            zeh            改为 GPIO 软 I2C
 * 2026-06-21       2.0            zeh           改用 ESP-IDF 硬件 I2C 400kHz
 * 2026-06-21       2.1            zeh         改为 LL 裸机接口，移除 read 懒初始化
 * 2026-06-21       2.2            zeh         M0/M1 均使用 100kHz，提高电机出波期间
 *                                                的 I2C 抗干扰余量
 * 2026-06-21       2.3            zeh         更正文件头：read 保留幂等懒初始化兜底、
 *                                                总线速率改写为 100 kHz，与代码一致
 * 2026-06-21       2.4            zeh         M0 I2C 提速至 400kHz（共享 I2C1，与
 *                                                BMI160 一致），M1 维持 100kHz
 * 2026-08-01       2.5            zeh         vendor 内部契约变更：config 以
 *                                                `const bm_hal_i2c_t *bus` 替代
 *                                                i2c_port/sda_gpio/scl_gpio 字段，
 *                                                read 改调 bm_hal_i2c_write_read；
 *                                                端口懒初始化上移总线后端
 * 2026-08-01       2.5            Codex            补全中文 Doxygen 合规注释
 */
#include "bm_vendor_encoder_esp32_idf.h"
#include "bm_hal_i2c_esp32_idf.h"
#include "bm_vendor_esp32_idf_compat.h"
#include "bm_types.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/** @brief 编码器实例数量。 */
#define BM_VENDOR_ENCODER_INSTANCE_COUNT  2u
/** @brief AS5600 设备地址。 */
#define BM_VENDOR_ENCODER_AS5600_ADDR     0x36u
/** @brief AS5600 RAW ANGLE 寄存器。 */
#define BM_VENDOR_ENCODER_AS5600_RAW_ANGLE_REG  0x0Cu

typedef struct {
    /** @brief 编号。 */
    uint32_t id;
    /** @brief 所在 I2C 总线设备实例。 */
    const bm_hal_i2c_t *bus;
} bm_vendor_encoder_config_t;

typedef struct {
    /** @brief 最近一次读到的角度。 */
    int32_t last_angle;
} bm_vendor_encoder_context_t;

static bm_vendor_encoder_context_t g_encoder_context[BM_VENDOR_ENCODER_INSTANCE_COUNT];

static const bm_vendor_encoder_config_t g_encoder_config_m0 = {
    0u,
    &bm_hal_i2c_1,
};

static const bm_vendor_encoder_config_t g_encoder_config_m1 = {
    1u,
    &bm_hal_i2c_0,
};

/**
 * @brief 取出设备上下文。
 */
static bm_vendor_encoder_context_t *bm_vendor_encoder_context_for(const struct bm_hal_encoder *dev) {
    const bm_vendor_encoder_config_t *cfg;

    if (dev == NULL || dev->config == NULL) {
        return NULL;
    }
    cfg = (const bm_vendor_encoder_config_t *)dev->config;
    if (cfg->id >= BM_VENDOR_ENCODER_INSTANCE_COUNT || cfg->bus == NULL) {
        return NULL;
    }
    return &g_encoder_context[cfg->id];
}

/**
 * @brief 读取编码器数值（有界事务）。
 *
 * 经 bm_hal_i2c_write_read 走 RESTART 寄存器读流程；端口初始化由
 * 总线后端首笔事务幂等懒初始化完成，失败返回平台错误码。
 */
static int bm_vendor_encoder_read(const struct bm_hal_encoder *dev, int32_t *value) {
    bm_vendor_encoder_context_t *ctx;
    const bm_vendor_encoder_config_t *cfg;
    uint8_t reg = BM_VENDOR_ENCODER_AS5600_RAW_ANGLE_REG;
    uint8_t buf[2];
    int rc;

    if (value == NULL) {
        return BM_ERR_INVALID;
    }
    ctx = bm_vendor_encoder_context_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    cfg = (const bm_vendor_encoder_config_t *)dev->config;

    rc = bm_hal_i2c_write_read(cfg->bus,
                               BM_VENDOR_ENCODER_AS5600_ADDR,
                               &reg, 1u,
                               buf, 2u);
    if (rc != BM_OK) {
        return rc;
    }

    *value = (int32_t)((((uint16_t)buf[0] << 8u) | (uint16_t)buf[1]) & 0x0FFFu);
    ctx->last_angle = *value;
    return BM_OK;
}

static const struct bm_encoder_driver_api g_encoder_api = {
    bm_vendor_encoder_read,
};

const bm_hal_encoder_t bm_hal_encoder_m0 = { &g_encoder_api, &g_encoder_config_m0 };
const bm_hal_encoder_t bm_hal_encoder_m1 = { &g_encoder_api, &g_encoder_config_m1 };
