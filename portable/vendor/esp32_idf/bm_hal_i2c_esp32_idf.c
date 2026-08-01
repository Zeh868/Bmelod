/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_i2c_esp32_idf.c
 * @brief ESP32-WROOM-32E I2C 总线后端实现（bm_hal_i2c 设备实例）
 * @maturity E1
 *
 * api 实现内部调用既有 LL 原语（bm_vendor_i2c_port_init/write/write_read，
 * 降级为后端内部实现细节）；首笔事务前幂等懒初始化端口
 * （原 encoder read 路径的懒初始化模式上移进总线后端，
 * bm_vendor_i2c_port_init 本身幂等，重复调用零开销返回）。
 *
 * 实例定义使用 BM_DEVICE_DEFINE（死宏转正为推荐写法）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-08-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-08-01       1.0            zeh            新增（I2C 总线契约 ESP32 后端）
 *
 * 2026-08-01       1.0            Codex            补全中文 Doxygen 合规注释
 */
#include "bm_hal_i2c_esp32_idf.h"
#include "bm_vendor_i2c_esp32_idf.h"
#include "bm_hal_instances_esp32wroom32e.h"
#include "bm_types.h"

/**
 * @brief 首笔事务前幂等懒初始化端口（bm_vendor_i2c_port_init 幂等）。
 */
static int bm_hal_i2c_esp32_ensure_init(const struct bm_hal_i2c *dev) {
    const bm_i2c_config_esp32_t *cfg =
        (const bm_i2c_config_esp32_t *)dev->config;

    return bm_vendor_i2c_port_init(cfg->port, cfg->sda, cfg->scl,
                                   cfg->base.clock_hz);
}

/**
 * @brief 通过I2C发送数据。
 * @param dev I2C 设备实例。
 * @param addr I2C 从设备地址。
 * @param buf 待发送数据缓冲区。
 * @param len 缓冲区中的有效数据长度，单位为字节。
 * @return 成功返回 BM_OK；设备或参数无效时返回校验错误码，并透传底层 I2C 传输错误码。
 */
static int bm_hal_i2c_esp32_write(const struct bm_hal_i2c *dev, uint8_t addr,
                                  const uint8_t *buf, size_t len) {
    const bm_i2c_config_esp32_t *cfg =
        (const bm_i2c_config_esp32_t *)dev->config;
    int rc = bm_hal_i2c_esp32_ensure_init(dev);

    if (rc != BM_OK) {
        return rc;
    }
    return bm_vendor_i2c_write(cfg->port, addr, buf, len,
                               cfg->base.timeout_ms);
}

/**
 * @brief 从I2C接收数据。
 * @param dev I2C 设备实例。
 * @param addr I2C 从设备地址。
 * @param buf 接收数据缓冲区。
 * @param len 缓冲区中的有效数据长度，单位为字节。
 * @return 成功返回 BM_OK；设备或参数无效时返回校验错误码，并透传底层 I2C 传输错误码。
 */
static int bm_hal_i2c_esp32_read(const struct bm_hal_i2c *dev, uint8_t addr,
                                 uint8_t *buf, size_t len) {
    const bm_i2c_config_esp32_t *cfg =
        (const bm_i2c_config_esp32_t *)dev->config;
    int rc = bm_hal_i2c_esp32_ensure_init(dev);

    if (rc != BM_OK) {
        return rc;
    }
    /* LL 无独立 read 原语：以零长写段 + 读段的 write_read 实现纯读
     *（START→addr(W)→RESTART→addr(R)→buf→STOP） */
    return bm_vendor_i2c_write_read(cfg->port, addr, NULL, 0u, buf, len,
                                    cfg->base.timeout_ms);
}

/**
 * @brief 通过I2C发送数据。
 * @param dev I2C 设备实例。
 * @param addr I2C 从设备地址。
 * @param wbuf 待发送数据缓冲区。
 * @param wlen 写缓冲区数据长度，单位为字节。
 * @param rbuf 接收数据缓冲区。
 * @param rlen 读缓冲区容量，单位为字节。
 * @return 成功返回 BM_OK；设备或参数无效时返回校验错误码，并透传底层 I2C 传输错误码。
 */
static int bm_hal_i2c_esp32_write_read(const struct bm_hal_i2c *dev,
                                       uint8_t addr,
                                       const uint8_t *wbuf, size_t wlen,
                                       uint8_t *rbuf, size_t rlen) {
    const bm_i2c_config_esp32_t *cfg =
        (const bm_i2c_config_esp32_t *)dev->config;
    int rc = bm_hal_i2c_esp32_ensure_init(dev);

    if (rc != BM_OK) {
        return rc;
    }
    return bm_vendor_i2c_write_read(cfg->port, addr, wbuf, wlen, rbuf, rlen,
                                    cfg->base.timeout_ms);
}

static const struct bm_i2c_driver_api g_i2c_esp32_api = {
    bm_hal_i2c_esp32_write,
    bm_hal_i2c_esp32_read,
    bm_hal_i2c_esp32_write_read,
};

/** @brief I2C0 后端配置（M1 AS5600：GPIO23/GPIO5，100 kHz）。 */
static const bm_i2c_config_esp32_t g_i2c0_config = {
    { 100000u, 0u },
    I2C_NUM_0,
    (gpio_num_t)BM_ESP32WROOM32E_ENCODER1_SDA_GPIO,  /* GPIO23 */
    (gpio_num_t)BM_ESP32WROOM32E_ENCODER1_SCL_GPIO,  /* GPIO5 */
};

/** @brief I2C1 后端配置（M0 AS5600 + BMI160 共线：GPIO19/GPIO18，400 kHz）。 */
static const bm_i2c_config_esp32_t g_i2c1_config = {
    { 400000u, 0u },
    I2C_NUM_1,
    (gpio_num_t)BM_ESP32WROOM32E_ENCODER0_SDA_GPIO,  /* GPIO19 */
    (gpio_num_t)BM_ESP32WROOM32E_ENCODER0_SCL_GPIO,  /* GPIO18 */
};

BM_DEVICE_DEFINE(bm_hal_i2c_0, bm_hal_i2c_t, &g_i2c_esp32_api, &g_i2c0_config);
BM_DEVICE_DEFINE(bm_hal_i2c_1, bm_hal_i2c_t, &g_i2c_esp32_api, &g_i2c1_config);
