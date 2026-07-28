/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_can_stm32g4.h
 * @brief STM32G4 FDCAN1/FDCAN2 后端配置与实例声明
 *
 * App 通过 `bm_can_stm32g4_config_t` 指定 FDCANx/引脚/AF/波特率/Message RAM/IRQ；
 * Bmelod 不固定 FDCAN 编号与产品引脚。
 *
 * 当前实现基于 FDCAN 寄存器直接操作，不依赖 HAL；支持 Classic CAN，
 * CAN FD 作为可选能力暴露，数据段波特率由 App 在 dbtr 中配置。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 STM32G4 FDCAN1/FDCAN2 后端
 * 2026-07-28       1.1            zeh            动态 Message RAM 布局、扩展过滤器、
 *                                                真实时间戳、bus-off 恢复接口
 */
#ifndef BM_VENDOR_CAN_STM32G4_H
#define BM_VENDOR_CAN_STM32G4_H

#include "drv/bm_drv_can.h"

#include <stdint.h>

#include "stm32g4xx.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief FDCAN 位时序寄存器预计算值。
 *
 * 由 App 按目标波特率与采样点计算；后端直接写入 NBTP/DBTP。
 */
typedef struct {
    uint32_t prescaler;  /**< 分频值（1..512，寄存器值 = prescaler - 1） */
    uint32_t tseg1;      /**< 相位段 1（1..256） */
    uint32_t tseg2;      /**< 相位段 2（1..128） */
    uint32_t sjw;        /**< 同步跳转宽度（1..128） */
} bm_can_stm32g4_bit_timing_t;

/**
 * @brief STM32G4 FDCAN 平台配置。
 *
 * App 静态分配并填写；init 时由后端校验合法性。
 */
typedef struct {
    FDCAN_GlobalTypeDef *fdcan;     /**< FDCAN 寄存器基址（FDCAN1/FDCAN2） */
    uint32_t             rcc_apb1;  /**< RCC APB1 时钟使能位 */

    uint32_t tx_pin;        /**< TX 引脚编码（port<<4 | num） */
    uint32_t rx_pin;        /**< RX 引脚编码（port<<4 | num） */
    uint32_t gpio_af;       /**< GPIO 复用功能号 */

    bm_can_stm32g4_bit_timing_t nbtr; /**< 仲裁段位时序 */
    bm_can_stm32g4_bit_timing_t dbtr; /**< CAN FD 数据段位时序（fd_enabled=0 可忽略） */
    uint32_t fd_enabled;    /**< 非零：启用 CAN FD 数据段 */

    /**
     * @brief Message RAM 在全局 FDCAN Message RAM 中的 32-bit word 偏移。
     *
     * STM32G4 全局 Message RAM 基址 0x4000A400，总大小 2560 words（10KB）。
     * FDCAN1/2 的区域不可重叠。
     */
    uint32_t message_ram_offset;

    uint32_t std_filter_count;  /**< 标准过滤器数量 */
    uint32_t ext_filter_count;  /**< 扩展过滤器数量 */
    uint32_t rx_fifo0_count;    /**< RX FIFO0 元素数量 */
    uint32_t rx_fifo1_count;      /**< RX FIFO1 元素数量 */
    uint32_t tx_fifo_count;       /**< TX FIFO/Queue 元素数量 */
    uint32_t tx_event_fifo_count; /**< TX Event FIFO 元素数量；0 表示禁用 */

    /**
     * @brief RX/TX element 数据段大小编码（0=8,1=12,2=16,3=20,4=24,5=32,6=48,7=64 bytes）
     */
    uint32_t rx_elmt_size;
    uint32_t tx_elmt_size;

    IRQn_Type irqn;         /**< FDCAN 中断 0（Line 0） */
    uint32_t  irq_priority; /**< NVIC 优先级 */
} bm_can_stm32g4_config_t;

/** @brief 默认 FDCAN1 实例（PB8/PB9，AF9，500k Classic CAN）。 */
extern const struct bm_hal_can bm_stm32g4_can1;

/** @brief 默认 FDCAN2 实例（PB12/PB13，AF9，500k Classic CAN）。 */
extern const struct bm_hal_can bm_stm32g4_can2;

/**
 * @brief 手动从 bus-off 恢复
 *
 * 进入 INIT 模式、清零错误计数与 bus-off 标志、再退出 INIT。
 * 也可通过 `bm_hal_can_stop()` + `bm_hal_can_start()` 序列完成同样动作。
 *
 * @param dev FDCAN 设备实例
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法；BM_ERR_TIMEOUT 硬件未响应
 */
int bm_can_stm32g4_recover(const struct bm_hal_can *dev);

/**
 * @brief 查询当前配置的实际仲裁段波特率与采样点
 *
 * @param dev                 FDCAN 设备实例
 * @param bitrate_bps         输出实际波特率（bps）；可为 NULL
 * @param sample_pt_promille  输出采样点（千分比）；可为 NULL
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法
 */
int bm_can_stm32g4_get_bitrate(const struct bm_hal_can *dev,
                               uint32_t *bitrate_bps,
                               uint32_t *sample_pt_promille);

#ifdef __cplusplus
}
#endif

#endif /* BM_VENDOR_CAN_STM32G4_H */
