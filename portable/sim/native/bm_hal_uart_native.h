/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_uart_native.h
 * @brief native_sim UART 后端测试辅助接口
 *
 * 仅供 native_sim 单元测试使用，用于向 RX 注入字节、触发 IDLE 事件、
 * 注入错误及读取发送侧数据。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 native_sim UART 测试辅助
 */
#ifndef BM_HAL_UART_NATIVE_H
#define BM_HAL_UART_NATIVE_H

#include "hal/bm_hal_uart.h"

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief native_sim 默认控制台 UART 设备（实例 0）。 */
extern const bm_hal_uart_t bm_uart_default;
/** @brief native_sim 额外 UART 实例（实例 1，loopback/测试用）。 */
extern const bm_hal_uart_t bm_native_uart1;

/**
 * @brief 重置所有 native_sim UART 实例状态（测试用）。
 */
void bm_hal_uart_native_reset(void);

/**
 * @brief 向指定 UART 注入一个 RX 字节（测试用）。
 *
 * 若已设置 ring buffer，写入 ring buffer；否则丢弃。
 * 有 per-byte RX 回调时同步派发。
 *
 * @param dev UART 设备实例
 * @param c   注入字节
 */
void bm_hal_uart_native_put_rx(const bm_hal_uart_t *dev, uint8_t c);

/**
 * @brief 向指定 UART 注入多个 RX 字节（测试用）。
 *
 * @param dev  UART 设备实例
 * @param data 数据指针
 * @param len  字节数
 */
void bm_hal_uart_native_put_rx_data(const bm_hal_uart_t *dev,
                                    const uint8_t *data, size_t len);

/**
 * @brief 手动触发指定 UART 的 IDLE 事件（测试用）。
 *
 * 触发 rx_frame_callback(BM_UART_EVT_IDLE)。
 *
 * @param dev UART 设备实例
 */
void bm_hal_uart_native_fire_idle(const bm_hal_uart_t *dev);

/**
 * @brief 向指定 UART 注入错误事件（测试用）。
 *
 * @param dev   UART 设备实例
 * @param error 错误标志组合（BM_UART_ERR_*）
 */
void bm_hal_uart_native_inject_error(const bm_hal_uart_t *dev,
                                     uint32_t error);

/**
 * @brief 读取实例 1 已发送的字节数（测试用）。
 *
 * @return 发送字节数
 */
size_t bm_hal_uart_native_tx_count(void);

/**
 * @brief 读取实例 1 发送缓冲区的指定位置字节（测试用）。
 *
 * @param idx 索引（从 0 开始）
 * @return 该位置字节；越界返回 0
 */
uint8_t bm_hal_uart_native_tx_byte(size_t idx);

#ifdef __cplusplus
}
#endif

#endif /* BM_HAL_UART_NATIVE_H */
