/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_rs485.h
 * @brief RS485 半双工链路包装组件
 *
 * 架在 UART HAL 与 GPIO HAL 之上，负责 DE/RE 方向控制、发送前后保持时间、
 * 接收帧事件、半双工冲突检测与链路统计。组件只上报事件与统计，App 决定
 * 超时/冲突后的业务动作。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 RS485 包装组件
 */
#ifndef BM_RS485_H
#define BM_RS485_H

#include "hal/bm_hal_uart.h"
#include "hal/bm_hal_gpio.h"
#include "bm/common/bm_types.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct bm_rs485;
typedef struct bm_rs485 bm_rs485_t;

/** @brief 方向状态：接收 */
#define BM_RS485_DIR_RX  0u
/** @brief 方向状态：发送 */
#define BM_RS485_DIR_TX  1u
/** @brief 方向状态：发送尾保持（等待 UART TC 后切回 RX） */
#define BM_RS485_DIR_TX_TAIL 2u

/** @brief 错误标志：半双工冲突（TX 期间收到非预期数据） */
#define BM_RS485_ERR_COLLISION       (1u << 0u)
/** @brief 错误标志：接收帧被覆盖/截断 */
#define BM_RS485_ERR_FRAME_DROP      (1u << 1u)
/** @brief 错误标志：接收空闲超时 */
#define BM_RS485_ERR_RX_IDLE_TIMEOUT (1u << 2u)
/** @brief 错误标志：UART 底层错误 */
#define BM_RS485_ERR_UART            (1u << 3u)

/** @brief 最大接收帧长度（含环回测试场景）。 */
#define BM_RS485_MAX_FRAME_LEN 256u

/** @brief 配置（用户填写） */
typedef struct {
    const bm_hal_uart_t *uart;        /**< UART 设备实例 */
    const bm_hal_gpio_t *de_gpio;     /**< DE GPIO 设备；NULL 表示硬件自动 DE */
    uint32_t             de_pin;      /**< DE 引脚编码 */
    int                  de_active_high; /**< 非零：DE 高有效 */
    uint32_t             pre_delay_us;   /**< 发送前置延时（µs） */
    uint32_t             post_delay_us;  /**< 发送结束保持时间（µs） */
    uint32_t             rx_idle_timeout_us; /**< 接收空闲超时（µs），0 表示不检测 */
    int                  filter_echo;      /**< 非零：过滤本机发送回显 */
    int                  hardware_de;      /**< 非零：使用硬件自动 DE，忽略 de_gpio */
} bm_rs485_config_t;

/** @brief 资源回调（用户填写） */
typedef struct {
    /**
     * @brief 接收到完整帧回调
     *
     * @param rs485  RS485 实例
     * @param data   帧数据（组件内部缓冲，回调返回后可能失效）
     * @param len    帧长度
     * @param user   透传参数
     */
    void (*frame_rx_cb)(const struct bm_rs485 *rs485,
                        const uint8_t *data, size_t len, void *user);

    /**
     * @brief 错误事件回调
     *
     * @param rs485 RS485 实例
     * @param error 错误标志组合（BM_RS485_ERR_*）
     * @param user  透传参数
     */
    void (*error_cb)(const struct bm_rs485 *rs485,
                     uint32_t error, void *user);

    void *user; /**< 回调透传参数 */
} bm_rs485_resources_t;

/** @brief 链路统计 */
typedef struct {
    uint32_t tx_frame_count;        /**< 发送帧数 */
    uint32_t rx_frame_count;        /**< 接收帧数 */
    uint32_t tx_byte_count;         /**< 发送字节数 */
    uint32_t rx_byte_count;         /**< 接收字节数 */
    uint32_t collision_count;       /**< 冲突次数 */
    uint32_t frame_drop_count;      /**< 丢帧次数 */
    uint32_t rx_idle_timeout_count; /**< 接收空闲超时次数 */
    uint32_t last_errors;           /**< 最近一次错误标志 */
} bm_rs485_stats_t;

/** @brief 运行状态（组件维护） */
typedef struct {
    uint32_t dir;                 /**< 当前方向 BM_RS485_DIR_* */
    uint8_t  rx_buf[BM_RS485_MAX_FRAME_LEN]; /**< 当前接收帧缓冲 */
    size_t   rx_len;              /**< 当前接收帧长度 */
    uint64_t last_rx_us;          /**< 最近 RX 时间戳 */
    uint64_t tx_end_us;           /**< 期望发送结束时刻 */
    bm_rs485_stats_t stats;       /**< 统计 */
    int      echo_pending;        /**< 正在过滤回显 */
    size_t   echo_len;            /**< 待过滤回显长度 */
} bm_rs485_state_t;

/** @brief RS485 实例（用户静态分配） */
typedef struct bm_rs485 {
    bm_rs485_config_t    config;
    bm_rs485_resources_t resources;
    bm_rs485_state_t     state;
} bm_rs485_t;

/**
 * @brief 校验配置
 * @return BM_OK 合法；BM_ERR_INVALID 非法
 */
int bm_rs485_validate_config(const bm_rs485_config_t *config);

/**
 * @brief 初始化 RS485 组件
 *
 * 配置 UART ring buffer、RX 帧回调、TX 完成回调；将 DE 置为接收方向。
 *
 * @param rs485 实例指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法；其他平台错误码
 */
int bm_rs485_init(bm_rs485_t *rs485);

/**
 * @brief 复位状态（不清除配置）
 * @param rs485 实例指针；NULL 静默返回
 */
void bm_rs485_reset(bm_rs485_t *rs485);

/**
 * @brief 发送一帧数据（非阻塞，回调通知完成）
 *
 * @param rs485 RS485 实例
 * @param data  帧数据
 * @param len   帧长度
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法；BM_ERR_BUSY 正在发送；
 *         其他平台错误码
 */
int bm_rs485_send(bm_rs485_t *rs485, const uint8_t *data, size_t len);

/**
 * @brief 周期处理：方向尾保持、接收空闲超时检测
 *
 * 须在固定周期调用；时间基为 bm_uptime_us()。
 *
 * @param rs485 实例指针；NULL 静默返回
 */
void bm_rs485_poll(bm_rs485_t *rs485);

/**
 * @brief 读取统计信息
 * @param rs485 RS485 实例
 * @param stats 输出统计
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法
 */
int bm_rs485_get_stats(const bm_rs485_t *rs485, bm_rs485_stats_t *stats);

/**
 * @brief 查询当前方向
 * @param rs485 RS485 实例
 * @return BM_RS485_DIR_RX / TX / TX_TAIL；NULL 时返回 RX
 */
uint32_t bm_rs485_dir(const bm_rs485_t *rs485);

#ifdef __cplusplus
}
#endif

#endif /* BM_RS485_H */
