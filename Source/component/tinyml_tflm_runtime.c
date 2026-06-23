/**
 * @file tinyml_tflm_runtime.c
 * @brief TinyML TFLite Micro 运行时 E1 stub 实现
 *
 * 本文件是**有意设计的外部 TFLM 注入桩**：默认不链接任何 TFLM 库，
 * 所有算子调用通过 bm_tinyml_tflm_register_ops() 注册的回调表分发。
 * 未注册回调时 invoke 返回 BM_ERR_NOT_SUPPORTED；用户链接 TFLM 后
 * 替换回调实现即可激活真实推理路径，无需重编译本模块。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            TFLM runtime E1 stub
 * 2026-06-23       0.2            zeh            补 SPDX 与函数级 Doxygen
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "bm/component/tinyml_tflm_runtime.h"
#include "bm/common/bm_types.h"

#include <stddef.h>
#include <string.h>

/** @brief 模块级全局 TFLM 回调表（进程单例，由 register_ops 写入） */
static bm_tinyml_tflm_ops_t s_tflm_ops;
/** @brief 回调表有效标志：非 0 表示已通过 register_ops 注册 */
static int s_tflm_ops_valid;

/**
 * @brief 注册 TFLM 运行时回调表（覆盖先前注册；ops 可为 NULL 表示清空）
 *
 * @param ops 回调表指针；NULL 时清空并置无效
 * @return BM_OK 成功
 */
int bm_tinyml_tflm_register_ops(const bm_tinyml_tflm_ops_t *ops) {
    if (ops == NULL) {
        memset(&s_tflm_ops, 0, sizeof(s_tflm_ops));
        s_tflm_ops_valid = 0;
        return BM_OK;
    }

    s_tflm_ops = *ops;
    s_tflm_ops_valid = 1;
    return BM_OK;
}

/**
 * @brief 从 bm_tinyml_graph_t 导出桥接配置并初始化 runtime 视图
 *
 * @param runtime 运行时状态（不可为 NULL）
 * @param graph   源图（不可为 NULL）
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效
 */
int bm_tinyml_tflm_runtime_bind_graph(bm_tinyml_tflm_runtime_t *runtime,
                                      const bm_tinyml_graph_t *graph) {
    if (runtime == NULL || graph == NULL) {
        return BM_ERR_INVALID;
    }

    memset(runtime, 0, sizeof(*runtime));
    bm_tflm_bridge_export_arena(graph, &runtime->bridge);
    return BM_OK;
}

/**
 * @brief 按默认映射表注册图中出现的算子（需已 register_ops 且 register_op 非 NULL）
 *
 * @param runtime 已 bind 的 runtime（不可为 NULL）
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_NOT_SUPPORTED 回调未注册或算子无映射
 */
int bm_tinyml_tflm_runtime_register_graph_ops(bm_tinyml_tflm_runtime_t *runtime) {
    uint32_t i;

    if (runtime == NULL || runtime->bridge.nodes == NULL ||
        runtime->bridge.node_count == 0u) {
        return BM_ERR_INVALID;
    }
    if (!s_tflm_ops_valid || s_tflm_ops.register_op == NULL) {
        return BM_ERR_NOT_SUPPORTED;
    }

    for (i = 0u; i < runtime->bridge.node_count; ++i) {
        const bm_tinyml_graph_node_t *node = &runtime->bridge.nodes[i];
        int builtin_id = bm_tflm_bridge_lookup_op(node->op, NULL, 0u);
        int rc;

        if (builtin_id < 0) {
            return BM_ERR_NOT_SUPPORTED;
        }
        rc = s_tflm_ops.register_op(runtime->user_ctx, node->op, builtin_id);
        if (rc != BM_OK) {
            return rc;
        }
    }
    runtime->ops_registered = 1;
    return BM_OK;
}

/**
 * @brief 初始化 TFLM 解释器（调用 ops->init；无则 no-op 成功）
 *
 * @param runtime 运行时状态（不可为 NULL）
 * @return BM_OK 成功或无 init 回调；init 失败时返回 init 的错误码
 */
int bm_tinyml_tflm_runtime_init(bm_tinyml_tflm_runtime_t *runtime) {
    if (runtime == NULL) {
        return BM_ERR_INVALID;
    }
    if (!s_tflm_ops_valid || s_tflm_ops.init == NULL) {
        return BM_OK;
    }
    return s_tflm_ops.init(runtime->user_ctx, &runtime->bridge);
}

/**
 * @brief 执行推理（调用 ops->invoke；无则 BM_ERR_NOT_SUPPORTED）
 *
 * @param runtime 运行时状态（不可为 NULL）
 * @return BM_OK 成功；BM_ERR_NOT_SUPPORTED 无 invoke 回调；其他值来自 invoke
 */
int bm_tinyml_tflm_invoke(bm_tinyml_tflm_runtime_t *runtime) {
    if (runtime == NULL) {
        return BM_ERR_INVALID;
    }
    if (!s_tflm_ops_valid || s_tflm_ops.invoke == NULL) {
        return BM_ERR_NOT_SUPPORTED;
    }
    return s_tflm_ops.invoke(runtime->user_ctx);
}

/**
 * @brief 释放 runtime（调用 ops->fini）
 *
 * @param runtime 运行时状态（可为 NULL，直接返回）
 */
void bm_tinyml_tflm_runtime_fini(bm_tinyml_tflm_runtime_t *runtime) {
    if (runtime == NULL) {
        return;
    }
    if (s_tflm_ops_valid && s_tflm_ops.fini != NULL) {
        s_tflm_ops.fini(runtime->user_ctx);
    }
}
