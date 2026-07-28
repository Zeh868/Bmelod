/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_board_device.h
 * @brief Board 外设设备描述符标准
 *
 * 定义 Board 接入契约中的设备类型、能力位掩码及统一设备描述符。
 * 应用通过 `bm_board_device_t` 数组向框架注册外设实例，Bmelod 不保存
 * 产品引脚，只保存指向 HAL 设备实例与平台相关配置的指针。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 Board 接入契约
 *
 */
#ifndef BM_BOARD_DEVICE_H
#define BM_BOARD_DEVICE_H

#include "bm/common/bm_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/*  设备类型                                                                   */
/* -------------------------------------------------------------------------- */

/** @brief UART 设备 */
#define BM_BOARD_DEV_TYPE_UART   0x01u
/** @brief SPI 设备 */
#define BM_BOARD_DEV_TYPE_SPI    0x02u
/** @brief CAN/CAN-FD 设备 */
#define BM_BOARD_DEV_TYPE_CAN    0x03u
/** @brief 通用定时器 / 高精度定时器设备 */
#define BM_BOARD_DEV_TYPE_TIMER  0x04u
/** @brief GPIO 设备（通常整芯片一个实例） */
#define BM_BOARD_DEV_TYPE_GPIO   0x05u
/** @brief 非易失存储后端 */
#define BM_BOARD_DEV_TYPE_NVS    0x06u
/** @brief PWM 设备 */
#define BM_BOARD_DEV_TYPE_PWM    0x07u
/** @brief ADC 设备 */
#define BM_BOARD_DEV_TYPE_ADC    0x08u

/* -------------------------------------------------------------------------- */
/*  能力位掩码（与 bm_board_has_capability() 配套使用）                         */
/* -------------------------------------------------------------------------- */

/** @brief UART 能力 */
#define BM_CAP_UART      (1u << 0u)
/** @brief SPI 能力 */
#define BM_CAP_SPI       (1u << 1u)
/** @brief CAN/CAN-FD 能力 */
#define BM_CAP_CAN       (1u << 2u)
/** @brief Timer 能力 */
#define BM_CAP_TIMER     (1u << 3u)
/** @brief GPIO 能力 */
#define BM_CAP_GPIO      (1u << 4u)
/** @brief NVS 能力 */
#define BM_CAP_NVS       (1u << 5u)
/** @brief PWM 能力 */
#define BM_CAP_PWM       (1u << 6u)
/** @brief ADC 能力 */
#define BM_CAP_ADC       (1u << 7u)
/** @brief DMA 能力（由 board 表能力字段显式声明） */
#define BM_CAP_DMA       (1u << 8u)
/** @brief 中断能力（由 board 表能力字段显式声明） */
#define BM_CAP_IRQ       (1u << 9u)

/* -------------------------------------------------------------------------- */
/*  设备描述符                                                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief Board 外设设备描述符
 *
 * 应用静态分配并填写；`hal_dev` 指向具体的 `bm_hal_xxx_t` 实例，
 * `config` 指向平台相关的配置结构（引脚、DMA、IRQ、时钟等）。
 * `resource_tag` 为可选资源标签，用于启动期冲突检查：非零时任意两个
 * 设备不得共享同一标签。
 */
typedef struct {
    uint32_t type;          /**< 设备类型，见 BM_BOARD_DEV_TYPE_* */
    uint32_t instance;      /**< 应用分配的逻辑实例号（同一 type 内唯一） */
    const char *name;       /**< 可选设备名（NULL 表示无名） */
    const void *hal_dev;    /**< 指向 bm_hal_xxx_t 实例 */
    const void *config;     /**< 平台相关配置（引脚、DMA、IRQ、时钟等） */
    uint32_t resource_tag;  /**< 可选资源标签，非零时用于冲突检查 */
} bm_board_device_t;

/**
 * @brief Board 设备表
 */
typedef struct {
    const bm_board_device_t *devices; /**< 设备数组，应用静态分配 */
    uint32_t count;                   /**< 设备数量，须 > 0 */
    uint32_t capabilities;            /**< 应用显式声明的 board 级能力掩码 */
} bm_board_table_t;

#ifdef __cplusplus
}
#endif

#endif /* BM_BOARD_DEVICE_H */
