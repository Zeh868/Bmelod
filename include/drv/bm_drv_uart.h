/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_uart.h
 * @brief UART 设备驱动 API（统一实例模型）
 *
 * 全实例化：设备 {api, config} + 实例 vtable，无单例全局符号。
 * 各后端导出一个默认控制台设备 bm_uart_default（声明见 bm_hal_uart.h），
 * 其余 UART（TMC2209/RS485 等）由 vendor 以同型设备导出。
 *
 * 接口批 1 扩展：新增 IDLE/环形 RX 缓冲/可配置帧格式/错误统计/TX 完成回调/
 * flush/abort 能力。原有四函数保持兼容。
 *
 * @author zeh (china_qzh@163.com)
 * @version 3.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-12       1.0            zeh            正式发布（单例契约）
 * 2026-07-27       2.0            zeh            打破式全实例化：删除单例全局符号约定
 * 2026-07-28       3.0            zeh            接口批 1 扩展 IDLE/环形缓冲/错误统计等
 * 2026-07-28       3.1            zeh            明确 last_errors 为 read-clear 语义
 *
 */
#ifndef BM_DRV_UART_H
#define BM_DRV_UART_H

#include <stddef.h>
#include <stdint.h>

struct bm_hal_uart;

/** @brief 校验位：无校验 */
#define BM_UART_PARITY_NONE 0u
/** @brief 校验位：偶校验 */
#define BM_UART_PARITY_EVEN 1u
/** @brief 校验位：奇校验 */
#define BM_UART_PARITY_ODD  2u

/** @brief 停止位：1 位 */
#define BM_UART_STOPBITS_1  0u
/** @brief 停止位：2 位 */
#define BM_UART_STOPBITS_2  1u

/** @brief 数据位：8 位 */
#define BM_UART_DATABITS_8  8u
/** @brief 数据位：9 位 */
#define BM_UART_DATABITS_9  9u

/** @brief RX 帧事件：UART IDLE 线检测 */
#define BM_UART_EVT_IDLE      (1u << 0u)
/** @brief RX 帧事件：环形缓冲区满 */
#define BM_UART_EVT_RX_FULL   (1u << 1u)
/** @brief RX 帧事件：显式帧结束（由后端或协议层触发） */
#define BM_UART_EVT_FRAME_END (1u << 2u)

/** @brief 错误标志：overrun（硬件覆盖未读数据） */
#define BM_UART_ERR_OVERRUN  (1u << 0u)
/** @brief 错误标志：framing 错误 */
#define BM_UART_ERR_FRAMING  (1u << 1u)
/** @brief 错误标志：parity 错误 */
#define BM_UART_ERR_PARITY   (1u << 2u)
/** @brief 错误标志：软件环形缓冲区溢出 */
#define BM_UART_ERR_OVERFLOW (1u << 3u)
/** @brief 错误标志：RX noise */
#define BM_UART_ERR_NOISE    (1u << 4u)

/** @brief UART 运行配置（init 时传入 config） */
typedef struct {
    uint32_t baud;       /**< 波特率，0 表示使用后端默认值 */
    uint32_t parity;     /**< BM_UART_PARITY_* */
    uint32_t stop_bits;  /**< BM_UART_STOPBITS_* */
    uint32_t data_bits;  /**< BM_UART_DATABITS_* */
} bm_uart_config_t;

/** @brief UART 通信统计 */
typedef struct {
    uint32_t tx_count;          /**< 累计发送字节数 */
    uint32_t rx_count;          /**< 累计接收字节数 */
    uint32_t rx_overrun_count;  /**< 硬件 overrun 次数 */
    uint32_t rx_framing_count;  /**< framing 错误次数 */
    uint32_t rx_parity_count;   /**< parity 错误次数 */
    uint32_t rx_overflow_count; /**< 软件环形缓冲区溢出次数 */
    uint32_t rx_noise_count;    /**< noise 错误次数 */
    uint32_t last_errors;       /**< 最近一次错误标志组合（get_stats 读后清零，read-clear） */
} bm_uart_stats_t;

/** @brief TX 完成回调原型（UART TC，非 DMA 完成） */
typedef void (*bm_uart_tx_complete_callback_t)(const struct bm_hal_uart *dev,
                                               void *user);

/** @brief RX 帧/IDLE 事件回调原型
 *
 * @param dev   UART 设备实例
 * @param event 事件标志（BM_UART_EVT_*）
 * @param len   自上次帧事件以来累计接收字节数；BM_UART_EVT_IDLE 时含本次 IDLE 前全部字节
 * @param user  透传参数
 */
typedef void (*bm_uart_rx_frame_callback_t)(const struct bm_hal_uart *dev,
                                            uint32_t event, size_t len,
                                            void *user);

struct bm_uart_driver_api {
    int (*init)(const struct bm_hal_uart *dev, void *config);
    int (*send)(const struct bm_hal_uart *dev,
                const uint8_t *data, size_t len);
    size_t (*recv)(const struct bm_hal_uart *dev,
                   uint8_t *data, size_t max_len);
    void (*set_rx_callback)(const struct bm_hal_uart *dev,
                            void (*cb)(uint8_t c));

    /**
     * @brief 中止当前 TX/RX 操作（清零状态、丢弃未完成数据）
     * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端
     */
    int (*abort)(const struct bm_hal_uart *dev);

    /**
     * @brief 等待 TX 完全发送完成（UART TC）
     * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；BM_ERR_TIMEOUT 超时
     */
    int (*flush)(const struct bm_hal_uart *dev);

    /**
     * @brief 注册 TX 完成回调（NULL 取消）
     * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端
     */
    int (*set_tx_complete_callback)(const struct bm_hal_uart *dev,
                                    bm_uart_tx_complete_callback_t cb,
                                    void *user);

    /**
     * @brief 注册 RX 帧/IDLE 事件回调（NULL 取消）
     * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端
     */
    int (*set_rx_frame_callback)(const struct bm_hal_uart *dev,
                                 bm_uart_rx_frame_callback_t cb,
                                 void *user);

    /**
     * @brief 设置 RX 环形缓冲区（供中断/DMA 后端写入）
     *
     * 后端收到数据时写入 ring buffer，并在 IDLE/RX_FULL 时通过
     * rx_frame_callback 通知上层；上层也可通过 recv 读取。
     *
     * @param dev UART 设备实例
     * @param buf 缓冲区；NULL 或 len==0 时关闭 ring buffer
     * @param len 缓冲区字节数
     * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；BM_ERR_INVALID 参数非法
     */
    int (*set_rx_buffer)(const struct bm_hal_uart *dev,
                         uint8_t *buf, size_t len);

    /**
     * @brief 读取通信统计
     * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；BM_ERR_INVALID stats 为 NULL
     */
    int (*get_stats)(const struct bm_hal_uart *dev, bm_uart_stats_t *stats);
};

struct bm_hal_uart {
    const struct bm_uart_driver_api *api;
    const void                      *config;
};

#endif /* BM_DRV_UART_H */
