/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_i2c.h
 * @brief I2C 设备驱动 API（阻塞同步事务）
 *
 * 对齐 SPI 契约（bm_drv_spi.h）的裁减原则：v1 刻意不做异步/ISR 事务、
 * 不做多主/从模式、不做 10-bit 地址与 SMBus 扩展（无真实消费者）。
 * config 结构为契约级通用配置：SCL 时钟与单笔事务忙等预算。
 *
 * 后端扩展配置约定：dev->config 可指向首成员为 bm_i2c_config_t 的
 * 后端私有结构（携带引脚/端口号），分发层不触碰 config。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-08-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-08-01       1.0            zeh            新增（I2C 总线契约，接口批 2）
 * 2026-08-01       1.0            Codex           补全 Doxygen 合规注释
 *
 */
#ifndef BM_DRV_I2C_H
#define BM_DRV_I2C_H

#include "drv/bm_drv.h"
#include "bm/common/bm_types.h"

#include <stddef.h>
#include <stdint.h>

struct bm_hal_i2c;

/**
 * @brief I2C 设备通用配置（契约级）。
 */
typedef struct bm_i2c_config {
    uint32_t clock_hz;    /**< SCL 时钟（Hz），通常 100000 / 400000 */
    uint32_t timeout_ms;  /**< 单笔事务忙等预算（ms）；0 = 后端默认值 */
} bm_i2c_config_t;

struct bm_i2c_driver_api {
    /**
     * @brief 阻塞写（START→addr(W)→buf→STOP）
     * @param dev  I2C 设备实例
     * @param addr 7-bit 从机地址（不含 R/W 位）
     * @param buf  发送缓冲
     * @param len  发送字节数（>0）
     * @return BM_OK 成功；否则为平台错误码
     */
    int (*write)(const struct bm_hal_i2c *dev, uint8_t addr,
                 const uint8_t *buf, size_t len);
    /**
     * @brief 阻塞读（START→addr(R)→buf→STOP）
     * @param dev  I2C 设备实例
     * @param addr 7-bit 从机地址（不含 R/W 位）
     * @param buf  接收缓冲
     * @param len  接收字节数（>0）
     * @return BM_OK 成功；否则为平台错误码
     */
    int (*read)(const struct bm_hal_i2c *dev, uint8_t addr,
                uint8_t *buf, size_t len);
    /**
     * @brief 写后读（RESTART 寄存器读流程，AS5600/BMI160 的刚需形态）
     * @param dev  I2C 设备实例
     * @param addr 7-bit 从机地址（不含 R/W 位）
     * @param wbuf 写段缓冲（寄存器地址等）
     * @param wlen 写段字节数（>0）
     * @param rbuf 读段缓冲
     * @param rlen 读段字节数（>0）
     * @return BM_OK 成功；否则为平台错误码
     */
    int (*write_read)(const struct bm_hal_i2c *dev, uint8_t addr,
                      const uint8_t *wbuf, size_t wlen,
                      uint8_t *rbuf, size_t rlen);
};

struct bm_hal_i2c {
    const struct bm_i2c_driver_api *api;
    const void                     *config; /**< bm_i2c_config_t 或后端扩展配置 */
};

#endif /* BM_DRV_I2C_H */
