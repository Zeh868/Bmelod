/**
 * @file tinyml_tflm_runtime.c
 * @brief TinyML TFLite Micro 运行时 E1 stub 实现
 *
 * 回调表注册与 invoke 分发；默认无 TFLM 链接，invoke 返回 BM_ERR_NOT_SUPPORTED。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            TFLM runtime E1 stub
 */
#include "bm/component/tinyml_tflm_runtime.h"
#include "bm/common/bm_types.h"

#include <stddef.h>
#include <string.h>

static bm_tinyml_tflm_ops_t s_tflm_ops;
static int s_tflm_ops_valid;

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

int bm_tinyml_tflm_runtime_bind_graph(bm_tinyml_tflm_runtime_t *runtime,
                                      const bm_tinyml_graph_t *graph) {
    if (runtime == NULL || graph == NULL) {
        return BM_ERR_INVALID;
    }

    memset(runtime, 0, sizeof(*runtime));
    bm_tflm_bridge_export_arena(graph, &runtime->bridge);
    return BM_OK;
}

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

int bm_tinyml_tflm_runtime_init(bm_tinyml_tflm_runtime_t *runtime) {
    if (runtime == NULL) {
        return BM_ERR_INVALID;
    }
    if (!s_tflm_ops_valid || s_tflm_ops.init == NULL) {
        return BM_OK;
    }
    return s_tflm_ops.init(runtime->user_ctx, &runtime->bridge);
}

int bm_tinyml_tflm_invoke(bm_tinyml_tflm_runtime_t *runtime) {
    if (runtime == NULL) {
        return BM_ERR_INVALID;
    }
    if (!s_tflm_ops_valid || s_tflm_ops.invoke == NULL) {
        return BM_ERR_NOT_SUPPORTED;
    }
    return s_tflm_ops.invoke(runtime->user_ctx);
}

void bm_tinyml_tflm_runtime_fini(bm_tinyml_tflm_runtime_t *runtime) {
    if (runtime == NULL) {
        return;
    }
    if (s_tflm_ops_valid && s_tflm_ops.fini != NULL) {
        s_tflm_ops.fini(runtime->user_ctx);
    }
}
