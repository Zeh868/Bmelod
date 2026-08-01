/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_i2c.c
 * @brief I2C HAL 分发层（契约 → driver API）
 *
 * 未绑定后端（dev/api/成员为 NULL）时返回 BM_ERR_NOT_INIT
 * （对齐既有分发层模式，逐行对齐 bm_hal_spi.c）。
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
#include "bm_hal_i2c.h"
#include "bm_types.h"

int bm_hal_i2c_write(const bm_hal_i2c_t *i2c, uint8_t addr,
                     const uint8_t *buf, size_t len) {
    if (!i2c || !i2c->api || !i2c->api->write) {
        return BM_ERR_NOT_INIT;
    }
    if (!buf || len == 0u) {
        return BM_ERR_INVALID;
    }
    return i2c->api->write(i2c, addr, buf, len);
}

int bm_hal_i2c_read(const bm_hal_i2c_t *i2c, uint8_t addr,
                    uint8_t *buf, size_t len) {
    if (!i2c || !i2c->api || !i2c->api->read) {
        return BM_ERR_NOT_INIT;
    }
    if (!buf || len == 0u) {
        return BM_ERR_INVALID;
    }
    return i2c->api->read(i2c, addr, buf, len);
}

int bm_hal_i2c_write_read(const bm_hal_i2c_t *i2c, uint8_t addr,
                          const uint8_t *wbuf, size_t wlen,
                          uint8_t *rbuf, size_t rlen) {
    if (!i2c || !i2c->api || !i2c->api->write_read) {
        return BM_ERR_NOT_INIT;
    }
    if (!wbuf || wlen == 0u || !rbuf || rlen == 0u) {
        return BM_ERR_INVALID;
    }
    return i2c->api->write_read(i2c, addr, wbuf, wlen, rbuf, rlen);
}
