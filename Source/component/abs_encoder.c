/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file abs_encoder.c
 * @brief 绝对值编码器组件实现（AS5047P over bm_hal_spi）
 *
 * AS5047P 帧（datasheet “Serial Peripheral Interface”）：
 *   命令帧 16bit：bit15=偶校验（对 bit14:0 取偶校验）、bit14=R/W（1=读）、
 *                 bit13:0=寄存器地址；
 *   应答帧 16bit：bit15=偶校验、bit14=错误标志（1=前帧错）、bit13:0=数据。
 * 读为流水结构：发读命令的当帧应答是前令结果，须再发一帧（NOP）取回
 * 本次数据。
 * 寄存器：ANGLECOM 0x3FFF（14bit 角度）、DIAAGC 0x3FFC（诊断，
 * 含 MAGH=bit8? 按 datasheet DIAAGC 位域：bit10=MAGL、bit9=MAGH，
 * 本实现状态字统一映射 bit1=MAGH、bit0=MAGL、bit15=错误位）。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1 步进伺服栈）
 * 2026-08-01       1.0            Codex           补全 Doxygen 合规注释
 *
 */
#include "bm/component/abs_encoder.h"

#include <stddef.h>

/** @brief AS5047P 命令帧：读标志（bit14）。 */
#define BM_AS5047P_READ_BIT   (1u << 14)
/** @brief AS5047P 帧：偶校验位（bit15）。 */
#define BM_AS5047P_PARITY_BIT (1u << 15)
/** @brief AS5047P 应答帧：错误标志（bit14）。 */
#define BM_AS5047P_ERROR_BIT  (1u << 14)
/** @brief AS5047P 寄存器：NOP。 */
#define BM_AS5047P_REG_NOP      0x0000u
/** @brief AS5047P 寄存器：DIAAGC（诊断/AGC/磁强）。 */
#define BM_AS5047P_REG_DIAAGC   0x3FFCu
/** @brief AS5047P 寄存器：ANGLECOM（14bit 角度）。 */
#define BM_AS5047P_REG_ANGLECOM 0x3FFFu
/** @brief DIAAGC：MAGL（磁场过弱）位。 */
#define BM_AS5047P_DIAAGC_MAGL  (1u << 10)
/** @brief DIAAGC：MAGH（磁场过强）位。 */
#define BM_AS5047P_DIAAGC_MAGH  (1u << 9)

/** @brief 状态字映射：错误位。 */
#define BM_ABS_ENC_STATUS_ERR  (1u << 15)
/** @brief 状态字映射：MAGH。 */
#define BM_ABS_ENC_STATUS_MAGH (1u << 1)
/** @brief 状态字映射：MAGL。 */
#define BM_ABS_ENC_STATUS_MAGL (1u << 0)

/**
 * @brief 计算 16bit 帧的偶校验位（对 bit14:0 的 1 计数取偶）。
 */
static uint16_t bm_as5047p_parity(uint16_t frame)
{
    uint16_t v = frame & 0x7FFFu;
    uint16_t p = 0u;

    while (v != 0u) {
        p ^= (uint16_t)(v & 1u);
        v >>= 1;
    }
    return p;
}

/**
 * @brief 组命令帧（读）：地址 + 读标志 + 偶校验。
 */
static uint16_t bm_as5047p_cmd_read(uint16_t addr)
{
    uint16_t cmd = (addr & 0x3FFFu) | BM_AS5047P_READ_BIT;

    if (bm_as5047p_parity(cmd) != 0u) {
        cmd |= BM_AS5047P_PARITY_BIT;
    }
    return cmd;
}

/**
 * @brief 组命令帧（NOP，带偶校验）。
 */
static uint16_t bm_as5047p_cmd_nop(void)
{
    uint16_t cmd = BM_AS5047P_REG_NOP;

    if (bm_as5047p_parity(cmd) != 0u) {
        cmd |= BM_AS5047P_PARITY_BIT;
    }
    return cmd;
}

/**
 * @brief 单帧交换（16bit MSB first），返回应答帧。
 * @return BM_OK 成功；BM_ERR_INVALID 应答偶校验错；否则为 SPI 错误码。
 */
static int bm_as5047p_exchange(const bm_hal_spi_t *spi, uint16_t cmd,
                               uint16_t *resp)
{
    uint8_t tx[2];
    uint8_t rx[2];
    int     rc;

    tx[0] = (uint8_t)(cmd >> 8);
    tx[1] = (uint8_t)(cmd);
    rc = bm_hal_spi_transfer(spi, tx, rx, 2u);
    if (rc != BM_OK) {
        return rc;
    }
    *resp = (uint16_t)(((uint16_t)rx[0] << 8) | rx[1]);
    /* 应答偶校验（含错误位在内对 bit14:0 校验） */
    if (bm_as5047p_parity(*resp) != (uint16_t)((*resp >> 15) & 1u)) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

/**
 * @brief 流水读寄存器：发读命令 + 再发 NOP 取回数据帧。
 */
static int bm_as5047p_read_reg(const bm_hal_spi_t *spi, uint16_t addr,
                               uint16_t *data, uint16_t *err)
{
    uint16_t resp;
    int      rc;

    rc = bm_as5047p_exchange(spi, bm_as5047p_cmd_read(addr), &resp);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_as5047p_exchange(spi, bm_as5047p_cmd_nop(), &resp);
    if (rc != BM_OK) {
        return rc;
    }
    if (err != NULL) {
        *err = (uint16_t)((resp & BM_AS5047P_ERROR_BIT) != 0u);
    }
    *data = (uint16_t)(resp & 0x3FFFu);
    return BM_OK;
}

/**
 * @brief AS5047P read_angle：流水读 ANGLECOM（14bit）。
 */
static int bm_as5047p_read_angle(const struct bm_hal_abs_encoder *dev,
                                 uint16_t *raw)
{
    const bm_abs_encoder_as5047p_config_t *cfg;
    uint16_t data = 0u;
    int      rc;

    if (dev == NULL || dev->config == NULL || raw == NULL) {
        return BM_ERR_INVALID;
    }
    cfg = (const bm_abs_encoder_as5047p_config_t *)dev->config;
    if (cfg->spi == NULL) {
        return BM_ERR_INVALID;
    }
    rc = bm_as5047p_read_reg(cfg->spi, BM_AS5047P_REG_ANGLECOM, &data, NULL);
    if (rc != BM_OK) {
        return rc;
    }
    *raw = data;
    return BM_OK;
}

/**
 * @brief AS5047P read_status：流水读 DIAAGC，映射统一状态字
 *        （bit15=错误位，bit1=MAGH，bit0=MAGL）。
 */
static int bm_as5047p_read_status(const struct bm_hal_abs_encoder *dev,
                                  uint16_t *status)
{
    const bm_abs_encoder_as5047p_config_t *cfg;
    uint16_t data = 0u;
    uint16_t err = 0u;
    uint16_t st = 0u;
    int      rc;

    if (dev == NULL || dev->config == NULL || status == NULL) {
        return BM_ERR_INVALID;
    }
    cfg = (const bm_abs_encoder_as5047p_config_t *)dev->config;
    if (cfg->spi == NULL) {
        return BM_ERR_INVALID;
    }
    rc = bm_as5047p_read_reg(cfg->spi, BM_AS5047P_REG_DIAAGC, &data, &err);
    if (rc != BM_OK) {
        return rc;
    }
    if (err != 0u) {
        st |= BM_ABS_ENC_STATUS_ERR;
    }
    if ((data & BM_AS5047P_DIAAGC_MAGH) != 0u) {
        st |= BM_ABS_ENC_STATUS_MAGH;
    }
    if ((data & BM_AS5047P_DIAAGC_MAGL) != 0u) {
        st |= BM_ABS_ENC_STATUS_MAGL;
    }
    *status = st;
    return BM_OK;
}

/** @brief AS5047P 型号 vtable。 */
const bm_abs_encoder_api_t bm_abs_encoder_as5047p_api = {
    bm_as5047p_read_angle,
    bm_as5047p_read_status,
};

/* ---------- 薄分发（型号无关） ---------- */

int bm_abs_encoder_read_angle(const bm_hal_abs_encoder_t *dev, uint16_t *raw) {
    if (!dev || !dev->api || !dev->api->read_angle) {
        return BM_ERR_NOT_INIT;
    }
    if (!raw) {
        return BM_ERR_INVALID;
    }
    return dev->api->read_angle(dev, raw);
}

int bm_abs_encoder_read_status(const bm_hal_abs_encoder_t *dev, uint16_t *status) {
    if (!dev || !dev->api || !dev->api->read_status) {
        return BM_ERR_NOT_INIT;
    }
    if (!status) {
        return BM_ERR_INVALID;
    }
    return dev->api->read_status(dev, status);
}
