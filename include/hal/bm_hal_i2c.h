/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_i2c.h
 * @brief I2C HAL 接口
 *
 * 阻塞同步事务；具体硬件由平台实现绑定。
 * 地址语义统一为 7-bit（不含 R/W 位）。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-08-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-08-01       1.0            zeh            新增（I2C 总线契约，接口批 2）
 *
 */
#ifndef BM_HAL_I2C_H
#define BM_HAL_I2C_H

#include "drv/bm_drv_i2c.h"
#include "bm/common/bm_types.h"

#include <stddef.h>
#include <stdint.h>

typedef struct bm_hal_i2c bm_hal_i2c_t;

/**
 * @brief I2C 阻塞写（START→addr(W)→buf→STOP）
 *
 * @param i2c  I2C 设备实例
 * @param addr 7-bit 从机地址（不含 R/W 位）
 * @param buf  发送缓冲
 * @param len  发送字节数
 * @return BM_OK 成功；BM_ERR_NOT_INIT 设备/api/成员未绑定；
 *         BM_ERR_INVALID buf 为 NULL 或 len 为 0；否则透传平台错误码
 */
int bm_hal_i2c_write(const bm_hal_i2c_t *i2c, uint8_t addr,
                     const uint8_t *buf, size_t len);

/**
 * @brief I2C 阻塞读（START→addr(R)→buf→STOP）
 *
 * @param i2c  I2C 设备实例
 * @param addr 7-bit 从机地址（不含 R/W 位）
 * @param buf  接收缓冲
 * @param len  接收字节数
 * @return BM_OK 成功；BM_ERR_NOT_INIT 设备/api/成员未绑定；
 *         BM_ERR_INVALID buf 为 NULL 或 len 为 0；否则透传平台错误码
 */
int bm_hal_i2c_read(const bm_hal_i2c_t *i2c, uint8_t addr,
                    uint8_t *buf, size_t len);

/**
 * @brief I2C 写后读（RESTART 寄存器读流程）
 *
 * 先写 wlen 字节（寄存器地址等），RESTART 后读 rlen 字节，
 * AS5600/BMI160 等寄存器型器件的刚需形态。
 *
 * @param i2c  I2C 设备实例
 * @param addr 7-bit 从机地址（不含 R/W 位）
 * @param wbuf 写段缓冲
 * @param wlen 写段字节数
 * @param rbuf 读段缓冲
 * @param rlen 读段字节数
 * @return BM_OK 成功；BM_ERR_NOT_INIT 设备/api/成员未绑定；
 *         BM_ERR_INVALID 任一缓冲为 NULL 或任一段长度为 0；
 *         否则透传平台错误码
 */
int bm_hal_i2c_write_read(const bm_hal_i2c_t *i2c, uint8_t addr,
                          const uint8_t *wbuf, size_t wlen,
                          uint8_t *rbuf, size_t rlen);

#endif /* BM_HAL_I2C_H */
