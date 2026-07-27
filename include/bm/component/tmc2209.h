/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file tmc2209.h
 * @brief TMC2209 步进驱动器组件（Trinamic 单线 UART 协议，芯片无关）
 *
 * 架在实例 UART（bm_hal_uart 设备）上；半双工收发切换由设备的单线
 * 标志承担，本组件按 config.single_wire 在读请求后丢弃回环节字节。
 * 协议帧格式与 CRC8 按 TMC2209 datasheet（Trinamic）“UART Single Wire
 * Interface”章节实现；寄存器地址见各函数注释。
 *
 * 默认值零内置：电流/细分全部由业务 config 给出（set_current /
 * set_microsteps 显式调用）。
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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_TMC2209_H
#define BM_TMC2209_H

#include "bm_hal_uart.h"
#include "bm/common/bm_types.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief TMC2209 寄存器：GCONF（全局配置）。 */
#define BM_TMC2209_REG_GCONF       0x00u
/** @brief TMC2209 寄存器：GSTAT（全局状态）。 */
#define BM_TMC2209_REG_GSTAT       0x01u
/** @brief TMC2209 寄存器：IOIN（输入引脚状态，init 通讯校验用）。 */
#define BM_TMC2209_REG_IOIN        0x06u
/** @brief TMC2209 寄存器：IHOLD_IRUN（电流设置）。 */
#define BM_TMC2209_REG_IHOLD_IRUN  0x10u
/** @brief TMC2209 寄存器：CHOPCONF（斩波配置，含 MRES 细分域 bits27:24）。 */
#define BM_TMC2209_REG_CHOPCONF    0x6Cu
/** @brief TMC2209 寄存器：SG_RESULT（StallGuard 结果，10bit）。 */
#define BM_TMC2209_REG_SG_RESULT   0x41u

typedef struct {
    const bm_hal_uart_t *uart;   /**< UART 设备实例 */
    uint8_t slave_addr;              /**< 从机地址（0..3） */
    uint8_t single_wire;             /**< 非零：读请求后丢弃回环节字节 */
    float   rsense_ohm;              /**< 采样电阻（Ω），电流换算用 */
} bm_tmc2209_config_t;

typedef struct {
    /**
     * @brief 堵转上报点（SG_RESULT 跌破阈值沿触发；NULL 跳过，
     *        业务可在此转接 health_monitor）。
     */
    void (*stall_callback)(void *user, uint16_t sg_result);
    void    *user;
    uint16_t stall_threshold;        /**< SG_RESULT < 该值视为堵转 */
} bm_tmc2209_resources_t;

typedef struct {
    uint16_t sg_result;   /**< 最近一次 SG_RESULT */
    uint8_t  comm_ok;     /**< init 通讯校验通过 */
    int      stalled;     /**< 当前堵转状态（滞回） */
    uint32_t poll_count;  /**< poll 调用计数 */
} bm_tmc2209_state_t;

typedef struct {
    bm_tmc2209_config_t    config;
    bm_tmc2209_resources_t resources;
    bm_tmc2209_state_t     state;
} bm_tmc2209_axis_t;

/**
 * @brief Trinamic CRC8（多项式 0x07，初值 0x00，MSB 先行，datasheet
 *        “CRC Calculation”节）。暴露供测试与上层复用。
 */
uint8_t bm_tmc2209_crc8(const uint8_t *data, size_t len);

/**
 * @brief 校验配置（uart 非 NULL、slave_addr ≤ 3、rsense > 0）
 * @return BM_OK 合法；BM_ERR_INVALID 非法
 */
int bm_tmc2209_validate_config(const bm_tmc2209_config_t *config);

/**
 * @brief 初始化：读 IOIN 寄存器验证通讯链路（帧格式 + CRC 往返）
 * @return BM_OK 成功（state.comm_ok=1）；BM_ERR_INVALID 参数非法；
 *         BM_ERR_TIMEOUT 无应答；BM_ERR_INVALID 应答 CRC/格式错
 */
int bm_tmc2209_init(bm_tmc2209_axis_t *axis);

/**
 * @brief 写寄存器（8 字节写帧：sync/slave/reg|0x80/data32/CRC8）
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法；BM_ERR_NOT_INIT 未初始化
 */
int bm_tmc2209_write_reg(bm_tmc2209_axis_t *axis, uint8_t reg, uint32_t value);

/**
 * @brief 读寄存器（4 字节读请求 + 8 字节应答，校验 sync/地址/寄存器/CRC）
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法；BM_ERR_NOT_INIT 未初始化；
 *         BM_ERR_TIMEOUT 无应答；BM_ERR_INVALID 应答 CRC/格式错
 */
int bm_tmc2209_read_reg(bm_tmc2209_axis_t *axis, uint8_t reg, uint32_t *value);

/**
 * @brief 设置细分（CHOPCONF.MRES，0=256 细分 … 8=整步；读-改-写）
 * @return 同 bm_tmc2209_read_reg/write_reg
 */
int bm_tmc2209_set_microsteps(bm_tmc2209_axis_t *axis, uint8_t mres);

/**
 * @brief 设置电流（IHOLD_IRUN：ihold | irun<<8 | iholddelay<<16）
 * @return 同 bm_tmc2209_write_reg
 */
int bm_tmc2209_set_current(bm_tmc2209_axis_t *axis,
                           uint8_t ihold, uint8_t irun, uint8_t iholddelay);

/**
 * @brief 读 StallGuard 结果（SG_RESULT 低 10 位）
 * @return 同 bm_tmc2209_read_reg
 */
int bm_tmc2209_read_stallguard(bm_tmc2209_axis_t *axis, uint16_t *sg);

/**
 * @brief 周期轮询：读 SG_RESULT 缓存到 state；跌破阈值沿触发
 *        resources.stall_callback（NULL 静默跳过）
 * @param axis 轴实例；NULL 静默返回
 */
void bm_tmc2209_poll(bm_tmc2209_axis_t *axis);

#ifdef __cplusplus
}
#endif

#endif /* BM_TMC2209_H */
