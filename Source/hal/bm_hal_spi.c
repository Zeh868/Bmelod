/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_spi.c
 * @brief SPI HAL 分发层（契约 → driver API）
 *
 * 未绑定后端时返回 BM_ERR_NOT_INIT（对齐既有分发层模式）。
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
#include "bm_hal_spi.h"
#include "bm_types.h"

int bm_hal_spi_transfer(const bm_hal_spi_t *spi,
                        const uint8_t *tx, uint8_t *rx, size_t len) {
    if (!spi || !spi->api || !spi->api->transfer) {
        return BM_ERR_NOT_INIT;
    }
    if ((!tx && !rx) || len == 0u) {
        return BM_ERR_INVALID;
    }
    return spi->api->transfer(spi, tx, rx, len);
}

int bm_hal_spi_transfer_async(const bm_hal_spi_t *spi,
                              const uint8_t *tx, uint8_t *rx, size_t len,
                              bm_spi_transfer_done_fn_t done_cb, void *user) {
    if (!spi || !spi->api) {
        return BM_ERR_NOT_INIT;
    }
    if (!spi->api->transfer_async) {
        return BM_ERR_NOT_SUPPORTED;
    }
    if ((!tx && !rx) || len == 0u) {
        return BM_ERR_INVALID;
    }
    return spi->api->transfer_async(spi, tx, rx, len, done_cb, user);
}
