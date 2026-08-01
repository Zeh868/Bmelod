/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_uart.h
 * @brief UART HAL 接口（统一实例模型）
 *
 * 串口设备初始化、发送/接收、RX 字节回调、IDLE/帧事件、错误统计；
 * 设备由后端导出（默认控制台设备 bm_uart_default）。
 *
 * hard RT 剖面下阻塞收发与 RX 用户回调不可进入流式路径，HAL 直接拒绝或返回 0。
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 3.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布（单例 API）
 * 2026-06-15       1.1            zeh            hard RT 禁止阻塞 UART 路径
 * 2026-07-27       2.0            zeh            打破式全实例化，统一为 {api, config}
 * 2026-07-28       3.0            zeh            接口批 1 扩展 IDLE/环形缓冲/错误统计等
 * 2026-08-01       3.0            Codex           补全 Doxygen 合规注释
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
 * @brief 后端默认控制台 UART 设备（由各 portable 后端导出）
 */
extern const bm_hal_uart_t bm_uart_default;
#endif

/**
 * @brief 初始化 UART 设备
 *
 * @param uart   UART 设备实例
 * @param config 平台相关配置指针；NULL 用设备默认。可传 bm_uart_config_t。
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；否则为平台错误码
 */
int bm_hal_uart_init(const bm_hal_uart_t *uart, void *config);

/**
 * @brief 发送数据
 *
 * 异步后端：调用者须保证 `data` 缓冲区在 DMA/中断完成前保持有效；
 * 完成时机由 `bm_hal_uart_set_tx_complete_callback()` 回调或
 * `bm_hal_uart_flush()` 返回确定。发送期间再次调用返回 `BM_ERR_BUSY`。
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

/**
 * @brief 中止当前 TX/RX 操作
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端
 */
int bm_hal_uart_abort(const bm_hal_uart_t *uart);

/**
 * @brief 等待 TX 完全发送完成（UART TC）
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；BM_ERR_TIMEOUT 超时
 */
int bm_hal_uart_flush(const bm_hal_uart_t *uart);

/**
 * @brief 注册 TX 完成回调（NULL 取消）
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端
 */
int bm_hal_uart_set_tx_complete_callback(const bm_hal_uart_t *uart,
                                         bm_uart_tx_complete_callback_t cb,
                                         void *user);

/**
 * @brief 注册 RX 帧/IDLE 事件回调（NULL 取消）
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端
 */
int bm_hal_uart_set_rx_frame_callback(const bm_hal_uart_t *uart,
                                      bm_uart_rx_frame_callback_t cb,
                                      void *user);

/**
 * @brief 设置 RX 环形缓冲区
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；BM_ERR_INVALID 参数非法
 */
int bm_hal_uart_set_rx_buffer(const bm_hal_uart_t *uart,
                              uint8_t *buf, size_t len);

/**
 * @brief 读取通信统计
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；BM_ERR_INVALID stats 为 NULL
 */
int bm_hal_uart_get_stats(const bm_hal_uart_t *uart, bm_uart_stats_t *stats);

#endif
