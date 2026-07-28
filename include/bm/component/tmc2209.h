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
 * @version 1.2
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1 步进伺服栈）
 * 2026-07-28       1.1            zeh            P0：IFCNT 写确认、GSTAT、DRV_STATUS、斩波模式、离线检测
 * 2026-07-28       1.2            zeh            审查整改：接收改 bm_uptime_us 字节级超时、rx_retries 上限校验
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
/** @brief TMC2209 寄存器：GSTAT（全局状态，读即清）。 */
#define BM_TMC2209_REG_GSTAT       0x01u
/** @brief TMC2209 寄存器：IFCNT（写操作成功计数，8bit 回绕）。 */
#define BM_TMC2209_REG_IFCNT       0x02u
/** @brief TMC2209 寄存器：IOIN（输入引脚状态，init 通讯校验用）。 */
#define BM_TMC2209_REG_IOIN        0x06u
/** @brief TMC2209 寄存器：IHOLD_IRUN（电流设置）。 */
#define BM_TMC2209_REG_IHOLD_IRUN  0x10u
/** @brief TMC2209 寄存器：SG_RESULT（StallGuard 结果，10bit）。 */
#define BM_TMC2209_REG_SG_RESULT   0x41u
/** @brief TMC2209 寄存器：CHOPCONF（斩波配置，含 MRES 细分域 bits27:24）。 */
#define BM_TMC2209_REG_CHOPCONF    0x6Cu
/** @brief TMC2209 寄存器：DRV_STATUS（驱动器状态/故障）。 */
#define BM_TMC2209_REG_DRV_STATUS  0x6Fu
/** @brief TMC2209 寄存器：PWMCONF（StealthChop PWM 配置）。 */
#define BM_TMC2209_REG_PWMCONF     0x70u

/** @brief GCONF.en_spreadCycle（bit2）：1=SpreadCycle，0=StealthChop。 */
#define BM_TMC2209_GCONF_EN_SPREADCYCLE  (1u << 2)

/** @brief IOIN 版本域掩码（bits31:24，TMC2209 典型值 0x21）。 */
#define BM_TMC2209_IOIN_VERSION_MASK     (0xFFu << 24)
/** @brief TMC2209 IOIN 版本号（datasheet 典型值）。 */
#define BM_TMC2209_IOIN_VERSION_TMC2209  0x21u

/** @brief StealthChop 缺省 PWMCONF（pwm_autoscale 等，datasheet 上电默认）。 */
#define BM_TMC2209_PWMCONF_DEFAULT       0xC10D0024u

/** @brief 接收重试缺省（与实现内 BM_TMC2209_RX_RETRIES_DEFAULT 一致）。 */
#define BM_TMC2209_RX_RETRIES_DEFAULT    200u
/** @brief 接收重试配置上限（超时兜底计数，防止忙等无界）。 */
#define BM_TMC2209_RX_RETRIES_MAX        100000u
/** @brief 写后 IFCNT 确认重试缺省次数。 */
#define BM_TMC2209_WRITE_RETRIES_DEFAULT 3u
/** @brief 连续通讯失败离线阈值缺省。 */
#define BM_TMC2209_OFFLINE_THRESHOLD_DEFAULT 5u

/** @brief 斩波模式：StealthChop（静音）。 */
#define BM_TMC2209_CHOPPER_STEALTH  0u
/** @brief 斩波模式：SpreadCycle（力矩优先）。 */
#define BM_TMC2209_CHOPPER_SPREAD   1u

/** @brief GSTAT 解析结果（读即清）。 */
typedef struct {
    uint8_t reset;    /**< bit0：上电/复位标志 */
    uint8_t drv_err;  /**< bit1：驱动器错误 */
    uint8_t uv_cp;    /**< bit2：电荷泵欠压 */
} bm_tmc2209_gstat_t;

/** @brief DRV_STATUS 常用故障/状态域解析。 */
typedef struct {
    uint8_t  otpw;       /**< 过温预警 */
    uint8_t  ot;         /**< 过温关断 */
    uint8_t  s2ga;       /**< A 相对地短路 */
    uint8_t  s2gb;       /**< B 相对地短路 */
    uint8_t  s2vsa;      /**< A 相对电源短路 */
    uint8_t  s2vsb;      /**< B 相对电源短路 */
    uint8_t  ola;        /**< A 开路 */
    uint8_t  olb;        /**< B 开路 */
    uint8_t  stealth;    /**< StealthChop 指示 */
    uint8_t  stst;       /**< 静止指示 */
    uint8_t  cs_actual;  /**< 实际电流刻度 bits20:16 */
    uint32_t raw;        /**< 原始寄存器值 */
} bm_tmc2209_drv_status_t;

typedef struct {
    const bm_hal_uart_t *uart;   /**< UART 设备实例 */
    uint8_t slave_addr;              /**< 从机地址（0..3） */
    uint8_t single_wire;             /**< 非零：读请求后丢弃回环节字节 */
    float   rsense_ohm;              /**< 采样电阻（Ω），电流换算用 */
    uint32_t rx_retries;             /**< 接收超时兜底重试上限；0=缺省 200，最大 BM_TMC2209_RX_RETRIES_MAX */
    uint8_t  write_retries;          /**< 写后 IFCNT 确认重试；0=缺省 3 */
    uint8_t  offline_threshold;      /**< 连续失败离线阈值；0=缺省 5 */
    uint8_t  clear_gstat_on_init;    /**< 非零：init 成功后读清 GSTAT */
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
    uint16_t sg_result;       /**< 最近一次 SG_RESULT */
    uint8_t  comm_ok;         /**< init 通讯校验通过 */
    uint8_t  offline;         /**< 连续通讯失败达阈值后置位 */
    uint8_t  comm_fail_count; /**< 连续通讯失败计数（成功一次清零） */
    uint8_t  chopper_mode;    /**< 当前斩波模式（BM_TMC2209_CHOPPER_*） */
    int      stalled;         /**< 当前堵转状态（滞回） */
    uint32_t poll_count;      /**< poll 调用计数 */
    bm_tmc2209_gstat_t      gstat;       /**< 最近一次 GSTAT */
    bm_tmc2209_drv_status_t drv_status;  /**< 最近一次 DRV_STATUS */
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
 * @brief 校验配置（uart 非 NULL、slave_addr ≤ 3、rsense > 0、
 *        rx_retries ≤ BM_TMC2209_RX_RETRIES_MAX）
 * @return BM_OK 合法；BM_ERR_INVALID 非法
 */
int bm_tmc2209_validate_config(const bm_tmc2209_config_t *config);

/**
 * @brief 初始化：读 IOIN 验证通讯链路；可选读清 GSTAT
 * @return BM_OK 成功（state.comm_ok=1）；BM_ERR_INVALID 参数非法或 IOIN 版本不符；
 *         BM_ERR_TIMEOUT 无应答；BM_ERR_INVALID 应答 CRC/格式错
 */
int bm_tmc2209_init(bm_tmc2209_axis_t *axis);

/**
 * @brief 写寄存器（8 字节写帧）；写后读 IFCNT 确认递增，有限重试
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法；BM_ERR_NOT_INIT 未初始化或已离线；
 *         BM_ERR_IO IFCNT 确认失败
 */
int bm_tmc2209_write_reg(bm_tmc2209_axis_t *axis, uint8_t reg, uint32_t value);

/**
 * @brief 读寄存器（4 字节读请求 + 8 字节应答，校验 sync/地址/寄存器/CRC）
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法；BM_ERR_NOT_INIT 未初始化或已离线；
 *         BM_ERR_TIMEOUT 无应答；BM_ERR_INVALID 应答 CRC/格式错
 */
int bm_tmc2209_read_reg(bm_tmc2209_axis_t *axis, uint8_t reg, uint32_t *value);

/**
 * @brief 读 GSTAT（读即清相关位）并解析到 state.gstat
 * @param out 可选输出；NULL 时仅更新 state
 * @return 同 bm_tmc2209_read_reg
 */
int bm_tmc2209_read_gstat(bm_tmc2209_axis_t *axis, bm_tmc2209_gstat_t *out);

/**
 * @brief 读清 GSTAT（等价于 bm_tmc2209_read_gstat(axis, NULL)）
 * @return 同 bm_tmc2209_read_gstat
 */
int bm_tmc2209_clear_gstat(bm_tmc2209_axis_t *axis);

/**
 * @brief 读 DRV_STATUS 并解析常用故障位到 state.drv_status
 * @param out 可选输出；NULL 时仅更新 state
 * @return 同 bm_tmc2209_read_reg
 */
int bm_tmc2209_read_drv_status(bm_tmc2209_axis_t *axis,
                               bm_tmc2209_drv_status_t *out);

/**
 * @brief 设置斩波模式（GCONF.en_spreadCycle；StealthChop 时写缺省 PWMCONF）
 * @param mode BM_TMC2209_CHOPPER_STEALTH 或 BM_TMC2209_CHOPPER_SPREAD
 * @return 同 bm_tmc2209_write_reg
 */
int bm_tmc2209_set_chopper_mode(bm_tmc2209_axis_t *axis, uint8_t mode);

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
