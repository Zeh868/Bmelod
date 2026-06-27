/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file tinyml_tflm_bridge.h
 * @brief TinyML 与 TFLite Micro 集成桥接契约（纯头文件，无 TFLM 库依赖）
 *
 * 定义 bm_tinyml_op_t 到 TFLM BuiltinOperator 占位 ID 的映射、arena 导出视图
 * 与集成步骤注释。E1 仅为契约层，实际推理仍由用户链接 TFLM 并完成算子注册。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.5
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            TFLM 桥接契约与 arena 导出辅助
 * 2026-06-17       0.2            zeh            POOL 占位映射与 5 步对接清单
 * 2026-06-17       0.3            zeh            DEPTHWISE_CONV 占位映射
 * 2026-06-17       0.4            zeh            CONV_2D 1x1 占位映射
 * 2026-06-17       0.5            zeh            对接 tinyml_tflm_runtime.h
 *
 * @note 本头文件不引入 TFLM 头；BuiltinOperator 占位 ID 须与所用 TFLM 版本对齐后替换。
 */
#ifndef BM_TINYML_TFLM_BRIDGE_H
#define BM_TINYML_TFLM_BRIDGE_H

#include "bm/component/tinyml_adapter.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * TFLM BuiltinOperator 占位 ID（与 TensorFlow Lite Micro 2.x 常见枚举对齐示例）。
 * 集成前请对照所用 TFLM 版本 `micro/micro_mutable_op_resolver.h` 或 flatbuffer schema 校验。
 */
#define BM_TFLM_BUILTIN_QUANTIZE   114
#define BM_TFLM_BUILTIN_FULLY_CONNECTED  9
#define BM_TFLM_BUILTIN_DEQUANTIZE  6
#define BM_TFLM_BUILTIN_RELU        17
#define BM_TFLM_BUILTIN_SOFTMAX     25
#define BM_TFLM_BUILTIN_RESHAPE     22
#define BM_TFLM_BUILTIN_ADD         0
#define BM_TFLM_BUILTIN_MUL         18
#define BM_TFLM_BUILTIN_POOL_2D     40
#define BM_TFLM_BUILTIN_DEPTHWISE_CONV_2D  4
#define BM_TFLM_BUILTIN_CONV_2D            3

typedef struct {
    bm_tinyml_op_t  bm_op;
    int             tflm_builtin_id;
} bm_tflm_op_map_t;

/** 默认算子映射表（与 bm_tinyml_op_t 顺序无关，按 bm_op 查表） */
static const bm_tflm_op_map_t bm_tflm_default_op_map[] = {
    { BM_TINYML_OP_QUANTIZE,    BM_TFLM_BUILTIN_QUANTIZE },
    { BM_TINYML_OP_FC,          BM_TFLM_BUILTIN_FULLY_CONNECTED },
    { BM_TINYML_OP_DEQUANTIZE,  BM_TFLM_BUILTIN_DEQUANTIZE },
    { BM_TINYML_OP_RELU,        BM_TFLM_BUILTIN_RELU },
    { BM_TINYML_OP_SOFTMAX,     BM_TFLM_BUILTIN_SOFTMAX },
    { BM_TINYML_OP_FLATTEN,     BM_TFLM_BUILTIN_RESHAPE },
    { BM_TINYML_OP_ADD,         BM_TFLM_BUILTIN_ADD },
    { BM_TINYML_OP_MUL,         BM_TFLM_BUILTIN_MUL },
    { BM_TINYML_OP_MAXPOOL_2X2, BM_TFLM_BUILTIN_POOL_2D },
    { BM_TINYML_OP_DEPTHWISE_CONV2D, BM_TFLM_BUILTIN_DEPTHWISE_CONV_2D },
    { BM_TINYML_OP_CONV2D_1X1, BM_TFLM_BUILTIN_CONV_2D }
};

#define BM_TFLM_DEFAULT_OP_MAP_COUNT \
    (sizeof(bm_tflm_default_op_map) / sizeof(bm_tflm_default_op_map[0]))

typedef struct {
    uint8_t               *arena_base;
    uint32_t               arena_size;
    bm_tinyml_tensor_t    *tensors;
    uint32_t               tensor_count;
    bm_tinyml_graph_node_t *nodes;
    uint32_t               node_count;
} bm_tflm_bridge_config_t;

typedef struct {
    int8_t  *data;
    uint32_t byte_count;
    uint32_t dims[4];
    uint32_t ndim;
} bm_tflm_tensor_view_t;

/**
 * @brief 查找 bm_tinyml_op_t 对应的 TFLM BuiltinOperator 占位 ID
 *
 * @param bm_op 本仓库算子枚举
 * @param map 映射表（可为 NULL，则使用 bm_tflm_default_op_map）
 * @param map_count 表项数（map 为 NULL 时忽略）
 * @return TFLM BuiltinOperator ID；未找到返回 -1
 */
static inline int bm_tflm_bridge_lookup_op(bm_tinyml_op_t bm_op,
                                           const bm_tflm_op_map_t *map,
                                           uint32_t map_count) {
    uint32_t i;
    const bm_tflm_op_map_t *table = (map != NULL) ? map : bm_tflm_default_op_map;
    uint32_t count = (map != NULL) ? map_count : BM_TFLM_DEFAULT_OP_MAP_COUNT;

    for (i = 0u; i < count; ++i) {
        if (table[i].bm_op == bm_op) {
            return table[i].tflm_builtin_id;
        }
    }
    return -1;
}

/**
 * @brief 从 bm_tinyml_graph_t 导出 arena 与节点表视图（供 TFLM 自定义 resolver 接线）
 */
static inline void bm_tflm_bridge_export_arena(const bm_tinyml_graph_t *graph,
                                               bm_tflm_bridge_config_t *out) {
    if (graph == NULL || out == NULL) {
        return;
    }
    out->arena_base = (graph->arena != NULL) ? graph->arena->storage : NULL;
    out->arena_size = BM_TINYML_ARENA_MAX_BYTES;
    out->tensors = graph->tensors;
    out->tensor_count = graph->tensor_count;
    out->nodes = graph->nodes;
    out->node_count = graph->node_count;
}

/**
 * @brief 导出单个 tensor 的只读视图（不含 scale/zero_point，量化参数仍取自源 tensor）
 */
static inline void bm_tflm_bridge_tensor_view(const bm_tinyml_tensor_t *src,
                                              bm_tflm_tensor_view_t *dst) {
    uint32_t i;

    if (src == NULL || dst == NULL) {
        return;
    }
    dst->data = src->data;
    dst->byte_count = src->byte_count;
    dst->ndim = src->ndim;
    for (i = 0u; i < 4u; ++i) {
        dst->dims[i] = src->dims[i];
    }
}

/*
 * TFLM 集成 5 步清单（用户侧，E1 契约层）：
 *
 * 1. **Arena 对齐**：`bm_tflm_bridge_export_arena` 取得 `arena_base`/`arena_size`；
 *    TFLM `tensor_arena` 须满足 16 字节对齐，大小 ≥ 模型 GetArenaUsedBytes() + 余量。
 * 2. **Op Resolver 注册**：遍历 `bm_tflm_default_op_map`，对图中出现的 `bm_op` 调用
 *    `MicroMutableOpResolver::AddXxx()`；`BM_TFLM_BUILTIN_*` 占位 ID 须与所用 TFLM 版本校验。
 * 3. **Tensor 量化参数**：从 flatbuffer 或 `bm_tinyml_tensor_t` 拷贝 `scale`/`zero_point`、
 *    `dims` 与 `data` 指针至 TfLiteTensor；FC/POOL 的 per-channel 量化由用户模型决定。
 * 4. **Invoke**：`Interpreter::AllocateTensors()` 后循环 `Invoke()`；与
 *    `bm_tinyml_graph_run` native_sim 对照输出（同输入量化值下允许 ±1 LSB）。
 * 5. **与 bm_tinyml_graph 导出关系**：Demo `tinyml_graph` 的节点序/权重布局可作为
 *    手工 flatbuffer 或自定义 op 的参考；生产路径仍以 TFLM 模型文件为单一真相源。
 *
 * 6. **Runtime 接线**（见 `tinyml_tflm_runtime.h`）：
 *    `bm_tinyml_tflm_runtime_bind_graph` → `bm_tinyml_tflm_register_ops`（用户实现）
 *    → `bm_tinyml_tflm_runtime_register_graph_ops` → `bm_tinyml_tflm_runtime_init`
 *    → `bm_tinyml_tflm_invoke`。默认 stub 无 TFLM 链接，`invoke` 返回 BM_ERR_NOT_SUPPORTED。
 */

#ifdef __cplusplus
}
#endif

#endif /* BM_TINYML_TFLM_BRIDGE_H */
