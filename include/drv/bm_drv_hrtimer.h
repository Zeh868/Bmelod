/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_hrtimer.h
 * @brief 高精度 Timer 驱动 API
 *
 * 多实例设备模型：每个 `bm_hal_hrtimer` 绑定一路硬件 Timer。
 * App 通过 vendor 提供的配置结构指定实际 TIM/通道/IRQ。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增高精度 Timer 驱动契约
 */
#ifndef BM_DRV_HRTIMER_H
#define BM_DRV_HRTIMER_H

#include "bm/common/bm_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct bm_hal_hrtimer;

/** @brief 运行模式：周期定时 */
#define BM_HRTIMER_MODE_PERIODIC 0u
/** @brief 运行模式：单次定时 */
#define BM_HRTIMER_MODE_ONESHOT  1u

/** @brief Timer 统计信息 */
typedef struct {
    uint32_t deadline_miss_count; /**< deadline miss 累计次数 */
    uint32_t irq_count;           /**< ISR 触发累计次数 */
    uint32_t wrap_count;          /**< 计数器回绕累计次数 */
} bm_hrtimer_stats_t;

/**
 * @brief Timer 到期回调原型
 *
 * 于 ISR 上下文调用，须满足有界、非阻塞、不长时间占用。
 *
 * @param dev  触发回调的 Timer 设备实例
 * @param user 注册时透传的上下文指针
 */
typedef void (*bm_hrtimer_callback_t)(const struct bm_hal_hrtimer *dev,
                                      void *user);

/**
 * @brief 高精度 Timer 配置（通用层最小配置）
 *
 * vendor 配置可在此基础上扩展，init 时传入 `void *config`。
 */
typedef struct {
    uint32_t              freq_hz;   /**< 期望 Timer 时钟频率（Hz） */
    bm_hrtimer_callback_t callback;  /**< 到期回调；NULL 表示不回调 */
    void                 *user;      /**< 回调透传参数 */
} bm_hrtimer_config_t;

struct bm_hrtimer_driver_api {
    /**
     * @brief 初始化 Timer
     * @return BM_OK 成功；BM_ERR_INVALID 参数非法；其他平台错误码
     */
    int (*init)(const struct bm_hal_hrtimer *dev, void *config);

    /**
     * @brief 启动 Timer
     *
     * @param dev       Timer 设备实例
     * @param mode      BM_HRTIMER_MODE_PERIODIC 或 BM_HRTIMER_MODE_ONESHOT
     * @param period_us 周期/超时时间（µs），受 min/max 约束
     * @return BM_OK 成功；BM_ERR_INVALID 参数非法；BM_ERR_NOT_SUPPORTED 模式不支持
     */
    int (*start)(const struct bm_hal_hrtimer *dev,
                 uint32_t mode, uint32_t period_us);

    /**
     * @brief 停止 Timer
     */
    int (*stop)(const struct bm_hal_hrtimer *dev);

    /**
     * @brief 动态设置下一次比较值（µs）
     *
     * 用于 Output Compare 或运行中变速；不影响当前计数器。
     *
     * @return BM_OK 成功；BM_ERR_NOT_SUPPORTED 后端不支持动态改值
     */
    int (*set_compare)(const struct bm_hal_hrtimer *dev, uint32_t compare_us);

    /**
     * @brief 查询 Timer 计数频率（Hz）
     */
    uint32_t (*get_freq)(const struct bm_hal_hrtimer *dev);

    /**
     * @brief 查询分辨率（ns/tick）
     */
    uint32_t (*get_resolution_ns)(const struct bm_hal_hrtimer *dev);

    /**
     * @brief 查询最大支持周期（µs）
     */
    uint32_t (*get_max_period_us)(const struct bm_hal_hrtimer *dev);

    /**
     * @brief 查询最小支持周期（µs）
     */
    uint32_t (*get_min_period_us)(const struct bm_hal_hrtimer *dev);

    /**
     * @brief 读取统计信息
     * @return BM_OK 成功；BM_ERR_INVALID stats 为 NULL
     */
    int (*get_stats)(const struct bm_hal_hrtimer *dev,
                     bm_hrtimer_stats_t *stats);

    /**
     * @brief 注册/更新到期回调
     *
     * 允许运行期更换回调；cb 为 NULL 时取消回调。
     *
     * @return BM_OK 成功
     */
    int (*set_callback)(const struct bm_hal_hrtimer *dev,
                        bm_hrtimer_callback_t cb, void *user);
};

struct bm_hal_hrtimer {
    const struct bm_hrtimer_driver_api *api;
    const void                         *config;
};

#ifdef __cplusplus
}
#endif

#endif /* BM_DRV_HRTIMER_H */
