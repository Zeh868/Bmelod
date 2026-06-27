/**
 * @file tinyml_tflm_runtime.h
 * @brief TinyML TFLite Micro 运行时薄封装（E1 stub，无 TFLM 库依赖）
 *
 * 提供算子回调注册表与 invoke 入口；默认桩返回 BM_ERR_NOT_SUPPORTED。
 * 用户链接 TFLM 后替换 `bm_tinyml_tflm_register_ops` 注册的实现。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            TFLM runtime E1 stub 与回调表
 * 2026-06-23       0.2            zeh            补 SPDX 与函数级 Doxygen
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_TINYML_TFLM_RUNTIME_H
#define BM_TINYML_TFLM_RUNTIME_H

#include "bm/component/tinyml_tflm_bridge.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 单算子 TFLM 回调（用户侧绑定 MicroMutableOpResolver）
 *
 * @param user_ctx 用户上下文（可为 NULL）
 * @param bm_op 本仓库算子枚举
 * @param tflm_builtin_id TFLM BuiltinOperator 占位 ID
 * @return 成功；未支持或注册失败
 */
typedef int (*bm_tinyml_tflm_op_register_fn)(void *user_ctx,
                                             bm_tinyml_op_t bm_op,
                                             int tflm_builtin_id);

/**
 * @brief TFLM Interpreter 生命周期回调
 */
typedef struct {
    /** 注册 BuiltinOperator；可为 NULL（跳过） */
    bm_tinyml_tflm_op_register_fn register_op;
    /** AllocateTensors / 模型加载；可为 NULL */
    int (*init)(void *user_ctx, const bm_tflm_bridge_config_t *bridge);
    /** Invoke 单步推理；可为 NULL（默认 BM_ERR_NOT_SUPPORTED） */
    int (*invoke)(void *user_ctx);
    /** 释放资源；可为 NULL */
    void (*fini)(void *user_ctx);
} bm_tinyml_tflm_ops_t;

typedef struct {
    void                      *user_ctx;
    bm_tflm_bridge_config_t    bridge;
    int                        ops_registered;
} bm_tinyml_tflm_runtime_t;

/**
 * @brief 注册 TFLM 运行时回调表（覆盖先前注册；ops 可为 NULL 表示清空）
 *
 * @param ops 回调表（可为 NULL，表示无用户实现）
 * @return 成功
 */
int bm_tinyml_tflm_register_ops(const bm_tinyml_tflm_ops_t *ops);

/**
 * @brief 从 bm_tinyml_graph_t 导出桥接配置并初始化 runtime 视图
 *
 * @param runtime 运行时状态（不可为 NULL）
 * @param graph 源图（不可为 NULL）
 * @return 成功；参数无效
 */
int bm_tinyml_tflm_runtime_bind_graph(bm_tinyml_tflm_runtime_t *runtime,
                                      const bm_tinyml_graph_t *graph);

/**
 * @brief 按默认映射表注册图中出现的算子（需已 register_ops 且 register_op 非 NULL）
 *
 * @param runtime 已 bind 的 runtime（不可为 NULL）
 * @return 成功；未注册回调或图无效
 */
int bm_tinyml_tflm_runtime_register_graph_ops(bm_tinyml_tflm_runtime_t *runtime);

/**
 * @brief 初始化 TFLM 解释器（调用 ops->init；无则 no-op 成功）
 *
 * @param runtime 运行时状态（不可为 NULL）
 * @return 成功；init 失败或未支持
 */
int bm_tinyml_tflm_runtime_init(bm_tinyml_tflm_runtime_t *runtime);

/**
 * @brief 执行推理（调用 ops->invoke；无则 BM_ERR_NOT_SUPPORTED）
 *
 * @param runtime 运行时状态（不可为 NULL）
 * @return 成功；未注册 invoke
 */
int bm_tinyml_tflm_invoke(bm_tinyml_tflm_runtime_t *runtime);

/**
 * @brief 释放 runtime（调用 ops->fini）
 *
 * @param runtime 运行时状态（可为 NULL）
 */
void bm_tinyml_tflm_runtime_fini(bm_tinyml_tflm_runtime_t *runtime);

#ifdef __cplusplus
}
#endif

#endif /* BM_TINYML_TFLM_RUNTIME_H */
