/**
 * @file tinyml_tflm_runtime.h
 * @brief TinyML TFLite Micro 运行时薄封装（E1 stub，无 TFLM 库依赖）。
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.3
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            TFLM runtime E1 stub 与回调表
 * 2026-06-23       0.2            zeh            补 SPDX 与函数级 Doxygen
 * 2026-07-28       0.3            zeh            改依赖 bm/common TinyML 桥接契约
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_TINYML_TFLM_RUNTIME_H
#define BM_TINYML_TFLM_RUNTIME_H

#include "bm/common/bm_tinyml_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册一个 TFLM BuiltinOperator。
 * @return BM_OK 成功；其他值由用户回调定义并原样透传。
 */
typedef int (*bm_tinyml_tflm_op_register_fn)(void *user_ctx,
                                             bm_tinyml_op_t bm_op,
                                             int tflm_builtin_id);

/** @brief TFLM Interpreter 生命周期回调表。 */
typedef struct {
    bm_tinyml_tflm_op_register_fn register_op; /**< 可选算子注册回调。 */
    int (*init)(void *user_ctx, const bm_tflm_bridge_config_t *bridge); /**< 可选初始化回调。 */
    int (*invoke)(void *user_ctx); /**< 可选单步推理回调。 */
    void (*fini)(void *user_ctx); /**< 可选释放回调。 */
} bm_tinyml_tflm_ops_t;

/** @brief TFLM runtime 绑定状态。 */
typedef struct {
    void *user_ctx; /**< 用户上下文。 */
    bm_tflm_bridge_config_t bridge; /**< 已导出的共享 bridge 配置。 */
    int ops_registered; /**< 非 0 表示图算子已经注册。 */
} bm_tinyml_tflm_runtime_t;

/**
 * @brief 注册或清空 TFLM 回调表。
 * @param ops 回调表；NULL 时清空注册。
 * @return BM_OK。
 */
int bm_tinyml_tflm_register_ops(const bm_tinyml_tflm_ops_t *ops);

/**
 * @brief 从 TinyML 图绑定 runtime bridge 视图。
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效。
 */
int bm_tinyml_tflm_runtime_bind_graph(bm_tinyml_tflm_runtime_t *runtime,
                                      const bm_tinyml_graph_t *graph);

/**
 * @brief 按默认映射注册图中算子。
 * @return BM_OK 成功；BM_ERR_INVALID runtime 或图无效；BM_ERR_NOT_SUPPORTED
 *         未注册回调或算子无映射；其他值由 register_op 回调透传。
 */
int bm_tinyml_tflm_runtime_register_graph_ops(bm_tinyml_tflm_runtime_t *runtime);

/**
 * @brief 初始化 TFLM 解释器。
 * @return BM_OK 成功（没有 init 回调时为 no-op）；BM_ERR_INVALID runtime 无效；
 *         其他值由 init 回调透传。
 */
int bm_tinyml_tflm_runtime_init(bm_tinyml_tflm_runtime_t *runtime);

/**
 * @brief 执行一次推理。
 * @return BM_ERR_INVALID runtime 无效；BM_ERR_NOT_SUPPORTED 未注册 invoke；
 *         其他值由 invoke 回调透传（成功时通常为 BM_OK）。
 */
int bm_tinyml_tflm_invoke(bm_tinyml_tflm_runtime_t *runtime);

/**
 * @brief 释放 runtime。
 * @param runtime runtime 实例；为 NULL 时静默返回。
 */
void bm_tinyml_tflm_runtime_fini(bm_tinyml_tflm_runtime_t *runtime);

#ifdef __cplusplus
}
#endif

#endif /* BM_TINYML_TFLM_RUNTIME_H */
