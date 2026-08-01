/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_spi.h
 * @brief SPI HAL 接口
 *
 * 阻塞全双工传输；具体硬件由平台实现绑定。
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1）
 * 2026-08-01       1.0            zeh           补全 Doxygen 合规注释
 *
 */
#ifndef BM_HAL_SPI_H
#define BM_HAL_SPI_H

#include "drv/bm_drv_spi.h"
#include "bm/common/bm_types.h"

#include <stddef.h>
#include <stdint.h>

typedef struct bm_hal_spi bm_hal_spi_t;

/**
 * @brief 阻塞全双工 SPI 传输
 *
 * @param spi SPI 设备实例
 * @param tx  发送缓冲（NULL 表示只收，发 0xFF 占位）
 * @param rx  接收缓冲（NULL 表示只发，丢弃接收）
 * @param len 传输字节数
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；BM_ERR_INVALID 参数无效
 */
int bm_hal_spi_transfer(const bm_hal_spi_t *spi,
                        const uint8_t *tx, uint8_t *rx, size_t len);

/**
 * @brief 异步全双工 SPI 传输（可选能力，DMA 等）
 *
 * 启动即返回；完成后于 ISR 上下文调 done_cb(spi, user)，回调时
 * tx/rx 缓冲区所有权归还调用方。
 *
 * @param spi     SPI 设备实例
 * @param tx      发送缓冲（NULL 表示只收，发 0xFF 占位）
 * @param rx      接收缓冲（NULL 表示只发，丢弃接收）
 * @param len     传输字节数
 * @param done_cb 完成回调（可为 NULL，完成后仅内部收尾）
 * @param user    回调透传参数
 * @return BM_OK 已启动；BM_ERR_NOT_INIT 无后端；BM_ERR_NOT_SUPPORTED
 *         后端不支持异步；BM_ERR_INVALID 参数无效；BM_ERR_BUSY 上一笔未完成
 */
int bm_hal_spi_transfer_async(const bm_hal_spi_t *spi,
                              const uint8_t *tx, uint8_t *rx, size_t len,
                              bm_spi_transfer_done_fn_t done_cb, void *user);

#endif /* BM_HAL_SPI_H */
