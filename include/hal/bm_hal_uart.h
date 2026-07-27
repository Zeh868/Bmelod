/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_uart.h
 * @brief UART HAL 接口（统一实例模型）
 *
 * 串口设备初始化、发送/接收及 RX 字节回调注册；设备由后端导出
 * （默认控制台设备 bm_uart_default）。
 * hard RT 剖面下阻塞收发不可进入流式路径，HAL 直接拒绝或返回 0。
 * @author zeh (china_qzh@163.com)
 * @version 2.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布（单例 API）
 * 2026-06-15       1.1            zeh            hard RT 禁止阻塞 UART 契约
 * 2026-07-27       1.2            zeh            追加设备实例 API（过渡形态）
 * 2026-07-27       2.0            zeh            打破式全实例化：四函数全部实例化，
 *                                                删除单例 API 与过渡形态 bm_hal_uart_dev_*；
 *                                                后端导出默认控制台设备 bm_uart_default
 *
 */
#ifndef BM_HAL_UART_H
#define BM_HAL_UART_H

#include "drv/bm_drv_uart.h"

#include <stdint.h>
#include <stddef.h>

typedef struct bm_hal_uart bm_hal_uart_t;

#ifdef BM_DRV_HAS_BACKEND
/**
 * @brief 后端默认控制台 UART 设备（由各 portable 后端导出：
 *        native_sim/qemu 各 sim/esp32/stm32g4 各一）
 */
extern const bm_hal_uart_t bm_uart_default;
#endif

/**
 * @brief 初始化 UART 设备
 *
 * @param uart   UART 设备实例
 * @param config 平台相关配置指针（NULL 用设备默认）
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；否则为平台错误码
 */
int bm_hal_uart_init(const bm_hal_uart_t *uart, void *config);

/**
 * @brief 发送数据
 *
 * @param uart UART 设备实例
 * @param data 待发送数据缓冲区
 * @param len  发送字节数
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；hard RT 剖面返回
 *         BM_ERR_NOT_SUPPORTED；否则为平台错误码
 */
int bm_hal_uart_send(const bm_hal_uart_t *uart, const uint8_t *data, size_t len);

/**
 * @brief 非阻塞接收数据
 *
 * @param uart    UART 设备实例
 * @param data    接收缓冲区
 * @param max_len 缓冲区最大字节数
 * @return 实际接收的字节数；hard RT 剖面恒返回 0
 */
size_t bm_hal_uart_recv(const bm_hal_uart_t *uart, uint8_t *data, size_t max_len);

/**
 * @brief 注册 RX 单字节回调
 *
 * hard RT 剖面忽略非 NULL 回调，避免 ISR 用户回调进入流式路径。
 *
 * @param uart UART 设备实例
 * @param cb   每收到一字节时调用的回调；NULL 表示取消注册
 */
void bm_hal_uart_set_rx_callback(const bm_hal_uart_t *uart, void (*cb)(uint8_t c));

#endif
