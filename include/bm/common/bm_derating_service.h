/**
 * @file bm_derating_service.h
 * @brief 降额服务的零组件依赖契约
 *
 * 为需要驱动降额的组件提供统一服务接口；本头文件不依赖任何组件实现。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       0.1            zeh            初始降额服务契约
 * 2026-07-28       0.2            zeh            明确 configure 使用统一状态错误码
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_DERATING_SERVICE_H
#define BM_DERATING_SERVICE_H

#include "bm/common/bm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 降额服务配置值 */
typedef struct {
    float rate_per_s;       /**< 降额与恢复斜坡速率，须大于 0 */
    float recovery_time_s;  /**< 恢复前等待时间，须不小于 0 */
    float dt_s;             /**< 控制周期时间步长，须大于 0 */
    float target_factor;    /**< 故障时目标降额因子，范围 [0, 1] */
} bm_derating_service_config_t;

/**
 * @brief 配置降额服务回调
 *
 * @return BM_OK 成功；BM_ERR_* 配置或参数失败
 */
typedef int (*bm_derating_service_configure_fn)(
    void *context, const bm_derating_service_config_t *config);

/** @brief 降额服务无返回值操作回调 */
typedef void (*bm_derating_service_op_fn)(void *context);

/** @brief 获取当前降额因子回调 */
typedef float (*bm_derating_service_get_factor_fn)(const void *context);

/**
 * @brief 降额服务绑定
 *
 * 所有回调及 context 必须非 NULL；configure 返回 BM_OK 表示成功，
 * 失败时返回 BM_ERR_*。
 */
typedef struct {
    void                              *context;
    bm_derating_service_configure_fn   configure;
    bm_derating_service_op_fn          reset;
    bm_derating_service_op_fn          latch;
    bm_derating_service_op_fn          clear_request;
    bm_derating_service_op_fn          step;
    bm_derating_service_get_factor_fn  get_factor;
} bm_derating_service_t;

#ifdef __cplusplus
}
#endif

#endif /* BM_DERATING_SERVICE_H */
