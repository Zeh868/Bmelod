/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_can.h
 * @brief CAN/FDCAN 设备驱动 API（统一实例模型）
 *
 * 多实例设备模型：每个 `bm_hal_can` 绑定一路 CAN 控制器。
 * App 通过 vendor 配置指定引脚/AF/波特率/Message RAM/IRQ。
 *
 * 设计约束：
 * - ISR 只做帧搬运与标志处理，不解析业务协议。
 * - 所有缓冲静态分配，禁止运行期 malloc。
 * - 错误统计由 ISR 或状态查询路径更新。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 CAN/FDCAN 驱动契约
 */
#ifndef BM_DRV_CAN_H
#define BM_DRV_CAN_H

#include "bm/common/bm_types.h"

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct bm_hal_can;

/** @brief 帧标志：扩展帧 */
#define BM_CAN_FLAG_EXT    (1u << 0u)
/** @brief 帧标志：远程帧（RTR） */
#define BM_CAN_FLAG_RTR    (1u << 1u)
/** @brief 帧标志：CAN FD 帧 */
#define BM_CAN_FLAG_FD     (1u << 2u)
/** @brief 帧标志：CAN FD 比特率切换（BRS） */
#define BM_CAN_FLAG_BRS    (1u << 3u)

/** @brief 最大 DLC（Classic CAN 为 8；CAN FD 为 64） */
#define BM_CAN_MAX_DLC     64u
/** @brief 标准帧 ID 最大值 */
#define BM_CAN_STD_ID_MAX  0x7FFu
/** @brief 扩展帧 ID 最大值 */
#define BM_CAN_EXT_ID_MAX  0x1FFFFFFFu

/** @brief CAN 帧结构 */
typedef struct {
    uint32_t id;                   /**< 帧 ID（标准 11bit / 扩展 29bit） */
    uint32_t flags;                /**< BM_CAN_FLAG_* 组合 */
    uint8_t  dlc;                  /**< 数据长度（字节数，非 DLC 编码） */
    uint8_t  data[BM_CAN_MAX_DLC]; /**< 帧数据 */
    uint64_t timestamp_us;         /**< 接收/发送时间戳（µs） */
} bm_can_frame_t;

/** @brief 过滤器类型：范围过滤器（id_low <= id <= id_high） */
#define BM_CAN_FILTER_TYPE_RANGE   0u
/** @brief 过滤器类型：掩码过滤器（id & mask == filter） */
#define BM_CAN_FILTER_TYPE_MASK    1u
/** @brief 过滤器类型：列表过滤器（精确匹配 id） */
#define BM_CAN_FILTER_TYPE_LIST    2u

/** @brief 过滤器：标准帧 */
#define BM_CAN_FILTER_STD          0u
/** @brief 过滤器：扩展帧 */
#define BM_CAN_FILTER_EXT          1u

/** @brief 过滤器目标：RX FIFO0 */
#define BM_CAN_FILTER_FIFO0        0u
/** @brief 过滤器目标：RX FIFO1 */
#define BM_CAN_FILTER_FIFO1        1u

/** @brief 过滤器配置 */
typedef struct {
    uint32_t type;      /**< BM_CAN_FILTER_TYPE_* */
    uint32_t id_format; /**< BM_CAN_FILTER_STD / EXT */
    uint32_t fifo;      /**< BM_CAN_FILTER_FIFO* */
    uint32_t id;        /**< 过滤器 ID / 范围低 ID / 列表 ID */
    uint32_t mask;      /**< 掩码（MASK 类型）或范围高 ID（RANGE 类型） */
} bm_can_filter_t;

/** @brief 事件标志：TX 完成 */
#define BM_CAN_EVT_TX_COMPLETE     (1u << 0u)
/** @brief 事件标志：RX 帧 */
#define BM_CAN_EVT_RX              (1u << 1u)
/** @brief 事件标志：bus-off */
#define BM_CAN_EVT_BUS_OFF         (1u << 2u)
/** @brief 事件标志：bus-off 恢复 */
#define BM_CAN_EVT_BUS_OFF_RECOVER (1u << 3u)
/** @brief 事件标志：error passive */
#define BM_CAN_EVT_ERROR_PASSIVE   (1u << 4u)
/** @brief 事件标志：error warning */
#define BM_CAN_EVT_ERROR_WARNING   (1u << 5u)
/** @brief 事件标志：RX 溢出 */
#define BM_CAN_EVT_RX_OVERFLOW     (1u << 6u)
/** @brief 事件标志：TX 超时/失败 */
#define BM_CAN_EVT_TX_TIMEOUT      (1u << 7u)

/** @brief 能力标志：支持 CAN FD */
#define BM_CAN_CAP_FD              (1u << 0u)
/** @brief 能力标志：支持标准帧过滤器 */
#define BM_CAN_CAP_STD_FILTER      (1u << 1u)
/** @brief 能力标志：支持扩展帧过滤器 */
#define BM_CAN_CAP_EXT_FILTER      (1u << 2u)
/** @brief 能力标志：支持 RX FIFO0 */
#define BM_CAN_CAP_FIFO0           (1u << 3u)
/** @brief 能力标志：支持 RX FIFO1 */
#define BM_CAN_CAP_FIFO1           (1u << 4u)
/** @brief 能力标志：支持 TX FIFO/Queue */
#define BM_CAN_CAP_TX_FIFO         (1u << 5u)

/** @brief 通信统计 */
typedef struct {
    uint32_t tx_count;            /**< 发送帧数 */
    uint32_t rx_count;            /**< 接收帧数 */
    uint32_t tx_timeout_count;    /**< TX 超时/失败次数 */
    uint32_t rx_overflow_count;   /**< RX 溢出次数 */
    uint32_t bus_off_count;       /**< bus-off 进入次数 */
    uint32_t error_warning_count; /**< error warning 次数 */
    uint32_t error_passive_count; /**< error passive 次数 */
    uint32_t arbitration_lost_count; /**< 仲裁丢失次数 */
    uint32_t last_errors;         /**< 最近一次错误/事件标志 */
} bm_can_stats_t;

/** @brief RX 回调原型（ISR 上下文） */
typedef void (*bm_can_rx_callback_t)(const struct bm_hal_can *dev,
                                     const bm_can_frame_t *frame,
                                     void *user);

/** @brief 事件回调原型（ISR 上下文） */
typedef void (*bm_can_event_callback_t)(const struct bm_hal_can *dev,
                                        uint32_t event,
                                        void *user);

/** @brief CAN 运行配置（init 时传入 config） */
typedef struct {
    uint32_t bitrate;        /**< 仲裁段位率（bps） */
    uint32_t fd_bitrate;     /**< CAN FD 数据段位率（bps），0 表示禁用 FD */
    uint32_t capabilities;   /**< 期望能力（后端可能不支持全部） */
} bm_can_config_t;

struct bm_can_driver_api {
    /**
     * @brief 初始化 CAN 控制器
     * @return BM_OK 成功；BM_ERR_INVALID 参数非法；其他平台错误码
     */
    int (*init)(const struct bm_hal_can *dev, void *config);

    /**
     * @brief 启动 CAN 控制器（进入正常模式）
     * @return BM_OK 成功；BM_ERR_INVALID 状态非法；其他平台错误码
     */
    int (*start)(const struct bm_hal_can *dev);

    /**
     * @brief 停止 CAN 控制器（进入初始化模式）
     * @return BM_OK 成功；其他平台错误码
     */
    int (*stop)(const struct bm_hal_can *dev);

    /**
     * @brief 发送一帧（异步；成功表示已提交到控制器）
     * @return BM_OK 成功提交；BM_ERR_BUSY 发送队列满；BM_ERR_INVALID 参数非法
     */
    int (*send)(const struct bm_hal_can *dev, const bm_can_frame_t *frame);

    /**
     * @brief 添加硬件过滤器
     *
     * @param dev       CAN 设备
     * @param filter    过滤器配置
     * @param filter_id 输出过滤器索引（用于 remove_filter）
     * @return BM_OK 成功；BM_ERR_INVALID 参数非法；BM_ERR_NOT_SUPPORTED 后端不支持；
     *         BM_ERR_NO_MEM 过滤器资源耗尽
     */
    int (*add_filter)(const struct bm_hal_can *dev,
                      const bm_can_filter_t *filter,
                      uint32_t *filter_id);

    /**
     * @brief 移除硬件过滤器
     * @return BM_OK 成功；BM_ERR_INVALID filter_id 非法
     */
    int (*remove_filter)(const struct bm_hal_can *dev, uint32_t filter_id);

    /**
     * @brief 查询能力
     * @return 能力位掩码（BM_CAN_CAP_*）
     */
    uint32_t (*get_capabilities)(const struct bm_hal_can *dev);

    /**
     * @brief 读取统计信息
     * @return BM_OK 成功；BM_ERR_INVALID stats 为 NULL
     */
    int (*get_stats)(const struct bm_hal_can *dev, bm_can_stats_t *stats);

    /**
     * @brief 注册 RX 回调（NULL 取消）
     * @return BM_OK 成功
     */
    int (*set_rx_callback)(const struct bm_hal_can *dev,
                           bm_can_rx_callback_t cb, void *user);

    /**
     * @brief 注册事件回调（NULL 取消）
     * @return BM_OK 成功
     */
    int (*set_event_callback)(const struct bm_hal_can *dev,
                              bm_can_event_callback_t cb, void *user);
};

struct bm_hal_can {
    const struct bm_can_driver_api *api;
    const void                     *config;
};

#ifdef __cplusplus
}
#endif

#endif /* BM_DRV_CAN_H */
