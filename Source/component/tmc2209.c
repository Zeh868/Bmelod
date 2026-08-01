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
 * 接收为有界轮询：以 bm_uptime_us 做字节级超时，config.rx_retries 作兜底
 * 上限（路径必须有界；应答间字节延迟由时间窗口吸收）。
 * 写寄存器后读 IFCNT 确认写成功；连续通讯失败达阈值置 offline。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1 步进伺服栈）
 * 2026-07-28       1.1            zeh            P0：IFCNT 写确认、GSTAT、DRV_STATUS、斩波模式、离线检测
 * 2026-07-28       1.2            zeh            审查整改：recv_exact 改 bm_uptime_us 字节级超时（rx_retries 兜底）、validate_config 校验 rx_retries 上限
 * 2026-08-01       1.2            Codex           补全 Doxygen 合规注释
 *
 */
#include "bm/component/tmc2209.h"

#include "bm/common/bm_uptime.h"

#include <stddef.h>
#include <string.h>

/** @brief 帧同步字节（datasheet：sync nibble + reserved，恒 0x05）。 */
#define BM_TMC2209_SYNC        0x05u
/** @brief 写寄存器标志（reg 字节 bit7）。 */
#define BM_TMC2209_WRITE_BIT   0x80u
/** @brief 读应答中的主机地址字节（恒 0xFF）。 */
#define BM_TMC2209_MASTER_ADDR 0xFFu

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
 * @brief 取有效接收重试次数。
 */
static uint32_t bm_tmc2209_rx_retries(const bm_tmc2209_config_t *cfg)
{
    if (cfg == NULL || cfg->rx_retries == 0u) {
        return BM_TMC2209_RX_RETRIES_DEFAULT;
    }
    return cfg->rx_retries;
}

/**
 * @brief 取写后 IFCNT 确认重试次数。
 */
static uint8_t bm_tmc2209_write_retries(const bm_tmc2209_config_t *cfg)
{
    if (cfg == NULL || cfg->write_retries == 0u) {
        return BM_TMC2209_WRITE_RETRIES_DEFAULT;
    }
    return cfg->write_retries;
}

/**
 * @brief 取连续失败离线阈值。
 */
static uint8_t bm_tmc2209_offline_threshold(const bm_tmc2209_config_t *cfg)
{
    if (cfg == NULL || cfg->offline_threshold == 0u) {
        return BM_TMC2209_OFFLINE_THRESHOLD_DEFAULT;
    }
    return cfg->offline_threshold;
}

/** @brief 单字节接收时间窗口（µs，约 2 个串口字节时间 @9600bps）。 */
#define BM_TMC2209_BYTE_TIMEOUT_US 2000u

/**
 * @brief 从 UART 读满 len 字节（字节级超时 + 重试兜底，有界）。
 *
 * 每收到字节即刷新时间窗口；空转超过 BM_TMC2209_BYTE_TIMEOUT_US 或
 * 空轮询次数达 rx_retries 兜底上限即判超时，避免零延时忙等在实机上
 * 瞬时空转耗尽重试而恒超时。
 *
 * @return BM_OK 读满；BM_ERR_TIMEOUT 超时。
 */
static int bm_tmc2209_recv_exact(const bm_tmc2209_axis_t *axis,
                                 uint8_t *buf, size_t len)
{
    size_t   got = 0u;
    uint32_t retries = 0u;
    uint32_t max_retries = bm_tmc2209_rx_retries(&axis->config);
    uint64_t deadline = bm_uptime_us() + (uint64_t)BM_TMC2209_BYTE_TIMEOUT_US;

    while (got < len && retries < max_retries) {
        size_t n = bm_hal_uart_recv(axis->config.uart,
                                        buf + got, len - got);
        if (n == 0u) {
            if (bm_uptime_us() >= deadline) {
                break;
            }
            retries++;
        } else {
            got += n;
            deadline = bm_uptime_us() + (uint64_t)BM_TMC2209_BYTE_TIMEOUT_US;
        }
    }
    return (got == len) ? BM_OK : BM_ERR_TIMEOUT;
}

/**
 * @brief 通讯成功：清零连续失败计数。
 */
static void bm_tmc2209_comm_success(bm_tmc2209_axis_t *axis)
{
    if (axis == NULL) {
        return;
    }
    axis->state.comm_fail_count = 0u;
}

/**
 * @brief 通讯失败：递增计数，达阈值置 offline 并清 comm_ok。
 */
static void bm_tmc2209_comm_failure(bm_tmc2209_axis_t *axis)
{
    uint8_t threshold;

    if (axis == NULL) {
        return;
    }
    threshold = bm_tmc2209_offline_threshold(&axis->config);
    if (axis->state.comm_fail_count < 255u) {
        axis->state.comm_fail_count++;
    }
    if (axis->state.comm_fail_count >= threshold) {
        axis->state.offline = 1u;
        axis->state.comm_ok = 0u;
    }
}

/**
 * @brief 组件入口统一校验（axis/uart 非空且已 init 且未离线）。
 */
static int bm_tmc2209_check_ready(const bm_tmc2209_axis_t *axis)
{
    if (axis == NULL) {
        return BM_ERR_INVALID;
    }
    if (axis->state.comm_ok == 0u || axis->state.offline != 0u) {
        return BM_ERR_NOT_INIT;
    }
    return BM_OK;
}

/**
 * @brief 解析 GSTAT 原始值。
 */
static void bm_tmc2209_parse_gstat(uint32_t raw, bm_tmc2209_gstat_t *out)
{
    out->reset   = (uint8_t)((raw >> 0) & 1u);
    out->drv_err = (uint8_t)((raw >> 1) & 1u);
    out->uv_cp   = (uint8_t)((raw >> 2) & 1u);
}

/**
 * @brief 解析 DRV_STATUS 原始值。
 */
static void bm_tmc2209_parse_drv_status(uint32_t raw, bm_tmc2209_drv_status_t *out)
{
    out->raw       = raw;
    out->otpw      = (uint8_t)((raw >> 0) & 1u);
    out->ot        = (uint8_t)((raw >> 1) & 1u);
    out->s2ga      = (uint8_t)((raw >> 2) & 1u);
    out->s2gb      = (uint8_t)((raw >> 3) & 1u);
    out->s2vsa     = (uint8_t)((raw >> 4) & 1u);
    out->s2vsb     = (uint8_t)((raw >> 5) & 1u);
    out->ola       = (uint8_t)((raw >> 6) & 1u);
    out->olb       = (uint8_t)((raw >> 7) & 1u);
    out->cs_actual = (uint8_t)((raw >> 16) & 0x1Fu);
    out->stealth   = (uint8_t)((raw >> 30) & 1u);
    out->stst      = (uint8_t)((raw >> 31) & 1u);
}

/**
 * @brief 底层读寄存器（不更新通讯成败计数）。
 */
static int bm_tmc2209_read_reg_raw(bm_tmc2209_axis_t *axis, uint8_t reg,
                                   uint32_t *value)
{
    uint8_t req[4];
    uint8_t reply[8];
    uint8_t echo[4];
    int     rc;

    if (axis == NULL || value == NULL || (reg & BM_TMC2209_WRITE_BIT) != 0u) {
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

/**
 * @brief 底层写帧发送（不确认 IFCNT）。
 */
static int bm_tmc2209_send_write_frame(bm_tmc2209_axis_t *axis, uint8_t reg,
                                     uint32_t value)
{
    uint8_t frame[8];

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

int bm_tmc2209_validate_config(const bm_tmc2209_config_t *config) {
    if (config == NULL || config->uart == NULL
        || config->slave_addr > 3u || config->rsense_ohm <= 0.0f
        || config->rx_retries > BM_TMC2209_RX_RETRIES_MAX) {
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
    if (((ioin & BM_TMC2209_IOIN_VERSION_MASK) >> 24)
        != BM_TMC2209_IOIN_VERSION_TMC2209) {
        return BM_ERR_INVALID;
    }
    axis->state.comm_ok = 1u;

    if (axis->config.clear_gstat_on_init != 0u) {
        uint32_t gstat_raw;
        rc = bm_tmc2209_read_reg_raw(axis, BM_TMC2209_REG_GSTAT, &gstat_raw);
        if (rc != BM_OK) {
            axis->state.comm_ok = 0u;
            return rc;
        }
        bm_tmc2209_parse_gstat(gstat_raw, &axis->state.gstat);
    }
    return BM_OK;
}

int bm_tmc2209_write_reg(bm_tmc2209_axis_t *axis, uint8_t reg, uint32_t value) {
    uint8_t retries;
    uint8_t attempt;
    int     rc;

    rc = bm_tmc2209_check_ready(axis);
    if (rc != BM_OK) {
        return rc;
    }
    if ((reg & BM_TMC2209_WRITE_BIT) != 0u) {
        return BM_ERR_INVALID;
    }

    retries = bm_tmc2209_write_retries(&axis->config);
    for (attempt = 0u; attempt < retries; ++attempt) {
        uint32_t ifcnt_before = 0u;
        uint32_t ifcnt_after  = 0u;
        uint8_t  before;
        uint8_t  after;

        rc = bm_tmc2209_read_reg_raw(axis, BM_TMC2209_REG_IFCNT, &ifcnt_before);
        if (rc != BM_OK) {
            bm_tmc2209_comm_failure(axis);
            return rc;
        }
        before = (uint8_t)(ifcnt_before & 0xFFu);

        rc = bm_tmc2209_send_write_frame(axis, reg, value);
        if (rc != BM_OK) {
            bm_tmc2209_comm_failure(axis);
            return rc;
        }

        rc = bm_tmc2209_read_reg_raw(axis, BM_TMC2209_REG_IFCNT, &ifcnt_after);
        if (rc != BM_OK) {
            bm_tmc2209_comm_failure(axis);
            return rc;
        }
        after = (uint8_t)(ifcnt_after & 0xFFu);
        if (after == (uint8_t)(before + 1u)) {
            bm_tmc2209_comm_success(axis);
            return BM_OK;
        }
    }

    bm_tmc2209_comm_failure(axis);
    return BM_ERR_IO;
}

int bm_tmc2209_read_reg(bm_tmc2209_axis_t *axis, uint8_t reg, uint32_t *value) {
    int rc;

    rc = bm_tmc2209_check_ready(axis);
    if (rc != BM_OK) {
        return rc;
    }
    if (value == NULL) {
        return BM_ERR_INVALID;
    }
    rc = bm_tmc2209_read_reg_raw(axis, reg, value);
    if (rc != BM_OK) {
        bm_tmc2209_comm_failure(axis);
        return rc;
    }
    bm_tmc2209_comm_success(axis);
    return BM_OK;
}

int bm_tmc2209_read_gstat(bm_tmc2209_axis_t *axis, bm_tmc2209_gstat_t *out) {
    uint32_t raw = 0u;
    int      rc;

    rc = bm_tmc2209_read_reg(axis, BM_TMC2209_REG_GSTAT, &raw);
    if (rc != BM_OK) {
        return rc;
    }
    bm_tmc2209_parse_gstat(raw, &axis->state.gstat);
    if (out != NULL) {
        *out = axis->state.gstat;
    }
    return BM_OK;
}

int bm_tmc2209_clear_gstat(bm_tmc2209_axis_t *axis) {
    return bm_tmc2209_read_gstat(axis, NULL);
}

int bm_tmc2209_read_drv_status(bm_tmc2209_axis_t *axis,
                               bm_tmc2209_drv_status_t *out)
{
    uint32_t raw = 0u;
    int      rc;

    rc = bm_tmc2209_read_reg(axis, BM_TMC2209_REG_DRV_STATUS, &raw);
    if (rc != BM_OK) {
        return rc;
    }
    bm_tmc2209_parse_drv_status(raw, &axis->state.drv_status);
    if (out != NULL) {
        *out = axis->state.drv_status;
    }
    return BM_OK;
}

int bm_tmc2209_set_chopper_mode(bm_tmc2209_axis_t *axis, uint8_t mode) {
    uint32_t gconf;
    int      rc;

    if (axis == NULL
        || (mode != BM_TMC2209_CHOPPER_STEALTH
            && mode != BM_TMC2209_CHOPPER_SPREAD)) {
        return BM_ERR_INVALID;
    }
    rc = bm_tmc2209_read_reg(axis, BM_TMC2209_REG_GCONF, &gconf);
    if (rc != BM_OK) {
        return rc;
    }
    if (mode == BM_TMC2209_CHOPPER_SPREAD) {
        gconf |= BM_TMC2209_GCONF_EN_SPREADCYCLE;
    } else {
        gconf &= ~BM_TMC2209_GCONF_EN_SPREADCYCLE;
    }
    rc = bm_tmc2209_write_reg(axis, BM_TMC2209_REG_GCONF, gconf);
    if (rc != BM_OK) {
        return rc;
    }
    if (mode == BM_TMC2209_CHOPPER_STEALTH) {
        rc = bm_tmc2209_write_reg(axis, BM_TMC2209_REG_PWMCONF,
                                  BM_TMC2209_PWMCONF_DEFAULT);
        if (rc != BM_OK) {
            return rc;
        }
    }
    axis->state.chopper_mode = mode;
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
