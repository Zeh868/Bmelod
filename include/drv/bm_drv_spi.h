/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_spi.h
 * @brief SPI 设备驱动 API（阻塞全双工 transfer）
 *
 * 刻意不做异步/DMA/多从机调度（dma_stream 有真实消费者再议）。
 * config 结构为契约级通用配置：时钟、模式、CS（经 bm_hal_gpio 设备）。
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
#ifndef BM_DRV_SPI_H
#define BM_DRV_SPI_H

#include "drv/bm_drv.h"
#include "drv/bm_drv_gpio.h"
#include "bm/common/bm_types.h"

#include <stddef.h>
#include <stdint.h>

struct bm_hal_spi;

/** @brief SPI 模式 0（CPOL=0, CPHA=0）。 */
#define BM_SPI_MODE_0  0u
/** @brief SPI 模式 1（CPOL=0, CPHA=1）。 */
#define BM_SPI_MODE_1  1u
/** @brief SPI 模式 2（CPOL=1, CPHA=0）。 */
#define BM_SPI_MODE_2  2u
/** @brief SPI 模式 3（CPOL=1, CPHA=1）。 */
#define BM_SPI_MODE_3  3u

/**
 * @brief SPI 异步传输完成回调（ISR 上下文触发，FPU 守卫包裹；
 *        回调时 tx/rx 缓冲区所有权归还调用方）。
 */
typedef void (*bm_spi_transfer_done_fn_t)(const struct bm_hal_spi *dev,
                                          void *user);

/**
 * @brief SPI 设备通用配置（契约级，vendor 按此初始化硬件）。
 */
typedef struct bm_spi_config {
    uint32_t                  clock_hz;    /**< SCK 时钟（Hz） */
    uint8_t                   mode;        /**< BM_SPI_MODE_0..3 */
    const struct bm_hal_gpio *cs_gpio;     /**< CS 所在 GPIO 设备；NULL = 无 CS */
    uint32_t                  cs_pin;      /**< CS pin 编码（bm_drv_gpio.h） */
    uint8_t                   cs_managed;  /**< 非零：transfer 内自动拉低/拉高 CS */
} bm_spi_config_t;

struct bm_spi_driver_api {
    /**
     * @brief 阻塞全双工传输（tx/rx 可其一为 NULL 表示只发/只收）
     * @return BM_OK 成功；否则为平台错误码
     */
    int (*transfer)(const struct bm_hal_spi *dev,
                    const uint8_t *tx, uint8_t *rx, size_t len);
    /**
     * @brief 可选：异步传输（DMA 等）。
     *
     * 启动即返回，完成后于 ISR 上下文调 done_cb(dev, user)（FPU 守卫包裹），
     * 回调时 tx/rx 缓冲区所有权归还调用方。成员为 NULL = 不支持
     * （分发层返回 BM_ERR_NOT_SUPPORTED）。
     *
     * @return BM_OK 已启动；BM_ERR_BUSY 上一笔未完成；否则为平台错误码
     */
    int (*transfer_async)(const struct bm_hal_spi *dev,
                          const uint8_t *tx, uint8_t *rx, size_t len,
                          bm_spi_transfer_done_fn_t done_cb, void *user);
};

struct bm_hal_spi {
    const struct bm_spi_driver_api *api;
    const void                     *config; /**< bm_spi_config_t */
};

#endif /* BM_DRV_SPI_H */
