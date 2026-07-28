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
 * @version 1.3
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 Board 接入契约
 * 2026-07-28       1.1            zeh            扩展资源数组与能力位
 * 2026-07-28       1.2            zeh            新增 BM_BOARD_NAME_MAX 宏并注明
 *                                                设备名上限；MSG_RAM 语义注明按实例隔离
 * 2026-07-28       1.3            zeh            MSG_RAM 改为全局共享区间冲突检查
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
/** @brief GPIO EXTI 能力 */
#define BM_CAP_GPIO_EXTI (1u << 10u)
/** @brief UART DMA RX 能力 */
#define BM_CAP_UART_DMA_RX (1u << 11u)
/** @brief UART DMA TX 能力 */
#define BM_CAP_UART_DMA_TX (1u << 12u)
/** @brief Classic CAN 能力 */
#define BM_CAP_CAN_CLASSIC (1u << 13u)
/** @brief CAN FD 能力 */
#define BM_CAP_CAN_FD    (1u << 14u)
/** @brief NVS 原子写能力 */
#define BM_CAP_NVS_ATOMIC (1u << 15u)

/* -------------------------------------------------------------------------- */
/*  资源描述（用于 Board 级冲突检查）                                            */
/* -------------------------------------------------------------------------- */

/** @brief 资源类型：GPIO 引脚 */
#define BM_BOARD_RES_PIN       0u
/** @brief 资源类型：DMA 通道 */
#define BM_BOARD_RES_DMA       1u
/** @brief 资源类型：IRQ 向量 */
#define BM_BOARD_RES_IRQ       2u
/** @brief 资源类型：Timer 通道 */
#define BM_BOARD_RES_TIMER_CH  3u
/** @brief 资源类型：FDCAN Message RAM 区段 */
#define BM_BOARD_RES_MSG_RAM   4u
/** @brief 资源类型：GPIO 复用功能（AF） */
#define BM_BOARD_RES_AF        5u

/**
 * @brief 单条资源描述
 *
 * 用于描述设备占用的一个具体硬件资源。`flags` 含义依 `type` 而定：
 * - PIN：flags 保留为 0；periph_id=端口（0=A..6=G），index=引脚号 0..15。
 * - DMA：flags 保留为 0；periph_id=控制器（1=DMA1, 2=DMA2），index=通道 1..7。
 * - IRQ：flags 保留为 0；periph_id=0，index=IRQn 编号。
 * - TIMER_CH：flags 保留为 0；periph_id=TIM 实例号（如 2=TIM2），index=通道 1..4。
 * - MSG_RAM：index=全局 Message RAM word 起始偏移，flags=长度（words）；
 *   periph_id=FDCAN 实例号（1/2，仅文档用）。STM32G4 上 FDCAN1/2 共享同一块
 *   Message RAM，固定布局建议登记 [0,212)/[212,424)；任意两段区间重叠即冲突。
 * - AF：flags 保留为 0；periph_id=0，index=AF 编号 0..15。
 */
typedef struct {
    uint32_t type;       /**< 资源类型，见 BM_BOARD_RES_* */
    uint32_t periph_id;  /**< 外设/端口标识 */
    uint32_t index;      /**< 资源索引 */
    uint32_t flags;      /**< 类型相关标志/长度 */
} bm_board_resource_t;

/* -------------------------------------------------------------------------- */
/*  设备描述符                                                                 */
/* -------------------------------------------------------------------------- */

/** @brief 设备名长度上限（含 NUL 终止符）；可见字符最多 63 个 */
#define BM_BOARD_NAME_MAX 64u

/**
 * @brief Board 外设设备描述符
 *
 * 应用静态分配并填写；`hal_dev` 指向具体的 `bm_hal_xxx_t` 实例，
 * `config` 指向平台相关的配置结构（引脚、DMA、IRQ、时钟等）。
 * `resources` 为可选资源数组，用于启动期冲突检查（同 Pin、同 DMA 通道、
 * 同 IRQ、同 Timer 通道、同实例 FDCAN Message RAM 重叠等）。
 * `resource_tag` 为旧版单一资源标签，保留兼容；非零时仍参与唯一性检查。
 */
typedef struct {
    uint32_t type;                   /**< 设备类型，见 BM_BOARD_DEV_TYPE_* */
    uint32_t instance;               /**< 应用分配的逻辑实例号（同一 type 内唯一） */
    const char *name;                /**< 可选设备名（NULL 表示无名；上限 63 字符，见 BM_BOARD_NAME_MAX） */
    const void *hal_dev;             /**< 指向 bm_hal_xxx_t 实例 */
    const void *config;              /**< 平台相关配置（引脚、DMA、IRQ、时钟等） */
    const bm_board_resource_t *resources; /**< 资源数组；NULL 表示无 */
    uint32_t resource_count;         /**< 资源数组元素数 */
    uint32_t resource_tag;           /**< 可选旧版资源标签，非零时用于冲突检查 */
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
