/**
 * @file bm_hal_uart.h
 * @brief UART HAL 接口
 *
 * 串口初始化、阻塞发送/接收及 RX 字节回调注册。
 * hard RT 剖面下阻塞收发不可进入流式路径，HAL 直接拒绝或返回 0。
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-15       1.1            zeh            hard RT 禁止阻塞 UART 契约
 *
 */
#ifndef BM_HAL_UART_H
#define BM_HAL_UART_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief 初始化 UART 外设
 *
 * @param config 平台相关配置指针
 * @return BM_OK 成功；否则为平台错误码
 */
int bm_hal_uart_init(void *config);

/**
 * @brief 阻塞发送数据
 *
 * @param data 待发送数据缓冲区
 * @param len 发送字节数
 * @return BM_OK 成功；hard RT 剖面返回 BM_ERR_NOT_SUPPORTED；否则为平台错误码
 */
int bm_hal_uart_send(const uint8_t *data, size_t len);

/**
 * @brief 接收数据（可能阻塞或部分返回）
 *
 * @param data 接收缓冲区
 * @param max_len 缓冲区最大字节数
 * @return 实际接收的字节数；hard RT 剖面恒返回 0
 */
size_t bm_hal_uart_recv(uint8_t *data, size_t max_len);

/**
 * @brief 注册 RX 单字节回调
 *
 * hard RT 剖面忽略非 NULL 回调，避免 ISR 用户回调进入流式路径。
 *
 * @param cb 每收到一字节时调用的回调；NULL 表示取消注册
 */
void bm_hal_uart_set_rx_callback(void (*cb)(uint8_t c));

#endif
