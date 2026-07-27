/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file tmc2209.c
 * @brief TMC2209 步进驱动器组件实现（Trinamic 单线 UART 协议）
 *
 * 帧格式（TMC2209 datasheet “UART Single Wire Interface”）：
 *   写帧（8B）：0x05 | slave | reg|0x80 | data[31:24..7:0] | CRC8
 *   读请求（4B）：0x05 | slave | reg | CRC8
 *   读应答（8B）：0x05 | 0xFF（主机地址） | reg | data[31:24..7:0] | CRC8
 * CRC8：多项式 0x07、初值 0x00、MSB 先行（datasheet “CRC Calculation”）。
 * 单线拓扑下 UART 收发自环：读请求后先丢弃 4 字节回环再读应答
 * （config.single_wire 控制）。
 *
 * 接收为有界重试轮询（路径必须有界；应答间字节延迟由重试窗口吸收）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1 步进伺服栈）
 *
 */
#include "bm/component/tmc2209.h"

#include <stddef.h>
#include <string.h>

/** @brief 帧同步字节（datasheet：sync nibble + reserved，恒 0x05）。 */
#define BM_TMC2209_SYNC        0x05u
/** @brief 写寄存器标志（reg 字节 bit7）。 */
#define BM_TMC2209_WRITE_BIT   0x80u
/** @brief 读应答中的主机地址字节（恒 0xFF）。 */
#define BM_TMC2209_MASTER_ADDR 0xFFu
/** @brief 单字节接收的最大重试轮数（有界性保证）。 */
#define BM_TMC2209_RX_RETRIES  200u

uint8_t bm_tmc2209_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0u;
    size_t  i;
    int     bit;

    for (i = 0u; i < len; ++i) {
        uint8_t cur = data[i];
        for (bit = 7; bit >= 0; --bit) {
            uint8_t fb = (uint8_t)(((crc >> 7) & 1u) ^ ((cur >> bit) & 1u));
            crc = (uint8_t)(crc << 1);
            if (fb != 0u) {
                crc ^= 0x07u;
            }
        }
    }
    return crc;
}

/**
 * @brief 从 UART 读满 len 字节（有界重试）。
 * @return BM_OK 读满；BM_ERR_TIMEOUT 重试耗尽。
 */
static int bm_tmc2209_recv_exact(const bm_tmc2209_axis_t *axis,
                                 uint8_t *buf, size_t len)
{
    size_t   got = 0u;
    uint32_t retries = 0u;

    while (got < len && retries < BM_TMC2209_RX_RETRIES) {
        size_t n = bm_hal_uart_recv(axis->config.uart,
                                        buf + got, len - got);
        if (n == 0u) {
            retries++;
        } else {
            got += n;
        }
    }
    return (got == len) ? BM_OK : BM_ERR_TIMEOUT;
}

/**
 * @brief 组件入口统一校验（axis/uart 非空且已 init）。
 */
static int bm_tmc2209_check_ready(const bm_tmc2209_axis_t *axis)
{
    if (axis == NULL) {
        return BM_ERR_INVALID;
    }
    if (axis->state.comm_ok == 0u) {
        return BM_ERR_NOT_INIT;
    }
    return BM_OK;
}

int bm_tmc2209_validate_config(const bm_tmc2209_config_t *config) {
    if (config == NULL || config->uart == NULL
        || config->slave_addr > 3u || config->rsense_ohm <= 0.0f) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

int bm_tmc2209_init(bm_tmc2209_axis_t *axis) {
    uint32_t ioin = 0u;
    int      rc;

    if (axis == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_tmc2209_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    memset(&axis->state, 0, sizeof(axis->state));

    /* 通讯校验链路直接走底层收发（此时 comm_ok 未置位，绕过 check_ready） */
    {
        uint8_t req[4];
        uint8_t reply[8];
        uint8_t echo[4];

        req[0] = BM_TMC2209_SYNC;
        req[1] = axis->config.slave_addr;
        req[2] = BM_TMC2209_REG_IOIN;
        req[3] = bm_tmc2209_crc8(req, 3u);
        rc = bm_hal_uart_send(axis->config.uart, req, sizeof(req));
        if (rc != BM_OK) {
            return rc;
        }
        if (axis->config.single_wire != 0u) {
            rc = bm_tmc2209_recv_exact(axis, echo, sizeof(echo));
            if (rc != BM_OK) {
                return rc;
            }
        }
        rc = bm_tmc2209_recv_exact(axis, reply, sizeof(reply));
        if (rc != BM_OK) {
            return rc;
        }
        if (reply[0] != BM_TMC2209_SYNC
            || reply[1] != BM_TMC2209_MASTER_ADDR
            || reply[2] != BM_TMC2209_REG_IOIN
            || bm_tmc2209_crc8(reply, 7u) != reply[7]) {
            return BM_ERR_INVALID;
        }
        ioin = ((uint32_t)reply[3] << 24) | ((uint32_t)reply[4] << 16)
               | ((uint32_t)reply[5] << 8) | (uint32_t)reply[6];
    }
    (void)ioin;
    axis->state.comm_ok = 1u;
    return BM_OK;
}

int bm_tmc2209_write_reg(bm_tmc2209_axis_t *axis, uint8_t reg, uint32_t value) {
    uint8_t frame[8];
    int     rc;

    rc = bm_tmc2209_check_ready(axis);
    if (rc != BM_OK) {
        return rc;
    }
    if ((reg & BM_TMC2209_WRITE_BIT) != 0u) {
        return BM_ERR_INVALID;
    }
    frame[0] = BM_TMC2209_SYNC;
    frame[1] = axis->config.slave_addr;
    frame[2] = reg | BM_TMC2209_WRITE_BIT;
    frame[3] = (uint8_t)(value >> 24);
    frame[4] = (uint8_t)(value >> 16);
    frame[5] = (uint8_t)(value >> 8);
    frame[6] = (uint8_t)(value);
    frame[7] = bm_tmc2209_crc8(frame, 7u);
    return bm_hal_uart_send(axis->config.uart, frame, sizeof(frame));
}

int bm_tmc2209_read_reg(bm_tmc2209_axis_t *axis, uint8_t reg, uint32_t *value) {
    uint8_t req[4];
    uint8_t reply[8];
    uint8_t echo[4];
    int     rc;

    rc = bm_tmc2209_check_ready(axis);
    if (rc != BM_OK) {
        return rc;
    }
    if (value == NULL || (reg & BM_TMC2209_WRITE_BIT) != 0u) {
        return BM_ERR_INVALID;
    }
    req[0] = BM_TMC2209_SYNC;
    req[1] = axis->config.slave_addr;
    req[2] = reg;
    req[3] = bm_tmc2209_crc8(req, 3u);
    rc = bm_hal_uart_send(axis->config.uart, req, sizeof(req));
    if (rc != BM_OK) {
        return rc;
    }
    if (axis->config.single_wire != 0u) {
        rc = bm_tmc2209_recv_exact(axis, echo, sizeof(echo));
        if (rc != BM_OK) {
            return rc;
        }
    }
    rc = bm_tmc2209_recv_exact(axis, reply, sizeof(reply));
    if (rc != BM_OK) {
        return rc;
    }
    if (reply[0] != BM_TMC2209_SYNC
        || reply[1] != BM_TMC2209_MASTER_ADDR
        || reply[2] != reg
        || bm_tmc2209_crc8(reply, 7u) != reply[7]) {
        return BM_ERR_INVALID;
    }
    *value = ((uint32_t)reply[3] << 24) | ((uint32_t)reply[4] << 16)
             | ((uint32_t)reply[5] << 8) | (uint32_t)reply[6];
    return BM_OK;
}

int bm_tmc2209_set_microsteps(bm_tmc2209_axis_t *axis, uint8_t mres) {
    uint32_t chopconf;
    int      rc;

    if (axis == NULL || mres > 8u) {
        return BM_ERR_INVALID;
    }
    rc = bm_tmc2209_read_reg(axis, BM_TMC2209_REG_CHOPCONF, &chopconf);
    if (rc != BM_OK) {
        return rc;
    }
    /* MRES 域：CHOPCONF bits27:24（datasheet CHOPCONF 寄存器表） */
    chopconf = (chopconf & ~(0xFu << 24)) | ((uint32_t)mres << 24);
    return bm_tmc2209_write_reg(axis, BM_TMC2209_REG_CHOPCONF, chopconf);
}

int bm_tmc2209_set_current(bm_tmc2209_axis_t *axis,
                           uint8_t ihold, uint8_t irun, uint8_t iholddelay) {
    uint32_t val;

    if (axis == NULL || ihold > 31u || irun > 31u || iholddelay > 15u) {
        return BM_ERR_INVALID;
    }
    val = (uint32_t)ihold | ((uint32_t)irun << 8) | ((uint32_t)iholddelay << 16);
    return bm_tmc2209_write_reg(axis, BM_TMC2209_REG_IHOLD_IRUN, val);
}

int bm_tmc2209_read_stallguard(bm_tmc2209_axis_t *axis, uint16_t *sg) {
    uint32_t val;
    int      rc;

    if (sg == NULL) {
        return BM_ERR_INVALID;
    }
    rc = bm_tmc2209_read_reg(axis, BM_TMC2209_REG_SG_RESULT, &val);
    if (rc != BM_OK) {
        return rc;
    }
    *sg = (uint16_t)(val & 0x3FFu);
    axis->state.sg_result = *sg;
    return BM_OK;
}

void bm_tmc2209_poll(bm_tmc2209_axis_t *axis) {
    uint16_t sg;

    if (axis == NULL) {
        return;
    }
    axis->state.poll_count++;
    if (bm_tmc2209_read_stallguard(axis, &sg) != BM_OK) {
        return;
    }
    if (sg < axis->resources.stall_threshold) {
        if (axis->state.stalled == 0) {
            axis->state.stalled = 1;
            if (axis->resources.stall_callback != NULL) {
                axis->resources.stall_callback(axis->resources.user, sg);
            }
        }
    } else {
        axis->state.stalled = 0;
    }
}
