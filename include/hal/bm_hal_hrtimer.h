/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_hrtimer.h
 * @brief 高精度 Timer HAL 接口
 *
 * 多实例设备模型：每个 `bm_hal_hrtimer_t` 绑定一路硬件 Timer。
 * App 通过 vendor 提供的配置结构指定实际 TIM/通道/IRQ；Bmelod 不固定 TIM 编号。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增高精度 Timer HAL 契约
 */
#ifndef BM_HAL_HRTIMER_H
#define BM_HAL_HRTIMER_H

#include "drv/bm_drv_hrtimer.h"
#include "bm/common/bm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bm_hal_hrtimer bm_hal_hrtimer_t;

/**
 * @brief 初始化 Timer 实例
 *
 * @param dev    Timer 设备实例
 * @param config vendor 相关配置；NULL 时尝试使用 dev->config
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；BM_ERR_INVALID 参数非法；其他平台错误码
 */
int bm_hal_hrtimer_init(const bm_hal_hrtimer_t *dev, void *config);

/**
 * @brief 启动 Timer
 *
 * @param dev       Timer 设备实例
 * @param mode      BM_HRTIMER_MODE_PERIODIC 或 BM_HRTIMER_MODE_ONESHOT
 * @param period_us 周期/超时时间（µs），受后端 min/max 约束
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；BM_ERR_INVALID 参数非法；
 *         BM_ERR_NOT_SUPPORTED 模式不支持；其他平台错误码
 */
int bm_hal_hrtimer_start(const bm_hal_hrtimer_t *dev,
                         uint32_t mode, uint32_t period_us);

/**
 * @brief 停止 Timer
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；其他平台错误码
 */
int bm_hal_hrtimer_stop(const bm_hal_hrtimer_t *dev);

/**
 * @brief 动态设置下一次比较值（µs）
 *
 * 用于 Output Compare 或运行中变速；不影响当前计数器。
 *
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；BM_ERR_NOT_SUPPORTED 后端不支持动态改值
 */
int bm_hal_hrtimer_set_compare(const bm_hal_hrtimer_t *dev, uint32_t compare_us);

/**
 * @brief 查询 Timer 计数频率（Hz）
 * @return 0 表示无后端或无法查询
 */
uint32_t bm_hal_hrtimer_get_freq(const bm_hal_hrtimer_t *dev);

/**
 * @brief 查询分辨率（ns/tick）
 * @return 0 表示无后端或无法查询
 */
uint32_t bm_hal_hrtimer_get_resolution_ns(const bm_hal_hrtimer_t *dev);

/**
 * @brief 查询最大支持周期（µs）
 * @return 0 表示无后端或无法查询
 */
uint32_t bm_hal_hrtimer_get_max_period_us(const bm_hal_hrtimer_t *dev);

/**
 * @brief 查询最小支持周期（µs）
 * @return 0 表示无后端或无法查询
 */
uint32_t bm_hal_hrtimer_get_min_period_us(const bm_hal_hrtimer_t *dev);

/**
 * @brief 读取统计信息
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；BM_ERR_INVALID stats 为 NULL
 */
int bm_hal_hrtimer_get_stats(const bm_hal_hrtimer_t *dev,
                             bm_hrtimer_stats_t *stats);

/**
 * @brief 注册/更新到期回调
 *
 * 允许运行期更换回调；cb 为 NULL 时取消回调。
 *
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端
 */
int bm_hal_hrtimer_set_callback(const bm_hal_hrtimer_t *dev,
                                bm_hrtimer_callback_t cb, void *user);

#ifdef __cplusplus
}
#endif

#endif /* BM_HAL_HRTIMER_H */
