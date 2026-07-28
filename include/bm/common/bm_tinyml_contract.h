/**
 * @file bm_tinyml_contract.h
 * @brief TinyML 跨组件共享的数据与 TFLM 桥接契约。
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            下沉 arena、图、算子与 TFLM 桥接契约
 * 2026-07-28       1.1            zeh            补齐共享契约字段及 bridge helper 的 Doxygen
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_TINYML_CONTRACT_H
#define BM_TINYML_CONTRACT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BM_TINYML_ARENA_MAX_BYTES
#define BM_TINYML_ARENA_MAX_BYTES 4096u
#endif

/** @brief 静态 bump-pointer arena。 */
typedef struct {
    uint8_t storage[BM_TINYML_ARENA_MAX_BYTES]; /**< 静态存储池。 */
    uint32_t offset; /**< 当前分配偏移。 */
    uint32_t peak_bytes; /**< 历史峰值分配量。 */
} bm_tinyml_arena_t;

/** @brief int8 张量元数据。 */
typedef struct {
    int8_t *data; /**< 张量数据缓冲。 */
    uint32_t byte_count; /**< 数据缓冲字节数。 */
    uint32_t dims[4]; /**< 最多四维的维度值。 */
    uint32_t ndim; /**< 有效维数，范围 [1, 4]。 */
    float scale; /**< 量化缩放系数。 */
    int32_t zero_point; /**< 量化零点。 */
} bm_tinyml_tensor_t;

/** @brief 张量量化参数。 */
typedef struct {
    float scale; /**< 量化缩放系数。 */
    int32_t zero_point; /**< 量化零点。 */
} bm_tinyml_quant_params_t;

/** @brief 最小 TinyML 图的算子类型。 */
typedef enum {
    BM_TINYML_OP_QUANTIZE = 0,
    BM_TINYML_OP_FC,
    BM_TINYML_OP_DEQUANTIZE,
    BM_TINYML_OP_RELU,
    BM_TINYML_OP_SOFTMAX,
    BM_TINYML_OP_FLATTEN,
    BM_TINYML_OP_ADD,
    BM_TINYML_OP_MUL,
    BM_TINYML_OP_MAXPOOL_2X2,
    BM_TINYML_OP_DEPTHWISE_CONV2D,
    BM_TINYML_OP_CONV2D_1X1,
    BM_TINYML_OP_CONV2D
} bm_tinyml_op_t;

/** @brief 最小图节点与卷积超参数。 */
typedef struct {
    bm_tinyml_op_t op; /**< 算子类型。 */
    uint32_t input_tensor; /**< 第一输入张量索引。 */
    uint32_t input_tensor_b; /**< ADD/MUL 的第二输入索引。 */
    uint32_t output_tensor; /**< 输出张量索引。 */
    const int8_t *fc_weights; /**< FC/卷积权重。 */
    uint32_t fc_in_dim; /**< FC 输入维度或卷积输入通道。 */
    uint32_t fc_out_dim; /**< FC 输出维度或卷积输出通道。 */
    uint32_t conv_kh; /**< 卷积核高。 */
    uint32_t conv_kw; /**< 卷积核宽。 */
    uint32_t conv_sh; /**< 垂直步长。 */
    uint32_t conv_sw; /**< 水平步长。 */
    uint32_t conv_pad_top; /**< 顶部显式 padding。 */
    uint32_t conv_pad_bottom; /**< 底部显式 padding。 */
    uint32_t conv_pad_left; /**< 左侧显式 padding。 */
    uint32_t conv_pad_right; /**< 右侧显式 padding。 */
    const int32_t *conv_bias; /**< 可选卷积偏置。 */
} bm_tinyml_graph_node_t;

/** @brief 由用户静态分配的 TinyML 图。 */
typedef struct {
    bm_tinyml_graph_node_t *nodes; /**< 节点数组。 */
    uint32_t node_count; /**< 节点数量。 */
    bm_tinyml_arena_t *arena; /**< 静态 arena。 */
    bm_tinyml_tensor_t *tensors; /**< 张量数组。 */
    uint32_t tensor_count; /**< 张量数量。 */
} bm_tinyml_graph_t;

#define BM_TFLM_BUILTIN_QUANTIZE 114
#define BM_TFLM_BUILTIN_FULLY_CONNECTED 9
#define BM_TFLM_BUILTIN_DEQUANTIZE 6
#define BM_TFLM_BUILTIN_RELU 17
#define BM_TFLM_BUILTIN_SOFTMAX 25
#define BM_TFLM_BUILTIN_RESHAPE 22
#define BM_TFLM_BUILTIN_ADD 0
#define BM_TFLM_BUILTIN_MUL 18
#define BM_TFLM_BUILTIN_POOL_2D 40
#define BM_TFLM_BUILTIN_DEPTHWISE_CONV_2D 4
#define BM_TFLM_BUILTIN_CONV_2D 3

/** @brief TinyML 算子与 TFLM BuiltinOperator ID 的映射项。 */
typedef struct {
    bm_tinyml_op_t bm_op; /**< TinyML 算子。 */
    int tflm_builtin_id; /**< TFLM BuiltinOperator ID 载荷。 */
} bm_tflm_op_map_t;

/** @brief 可传给 TFLM Interpreter 初始化回调的桥接视图。 */
typedef struct {
    uint8_t *arena_base; /**< arena 起始地址。 */
    uint32_t arena_size; /**< arena 总字节数。 */
    bm_tinyml_tensor_t *tensors; /**< 张量数组。 */
    uint32_t tensor_count; /**< 张量数量。 */
    bm_tinyml_graph_node_t *nodes; /**< 节点数组。 */
    uint32_t node_count; /**< 节点数量。 */
} bm_tflm_bridge_config_t;

/** @brief 无量化参数的轻量张量视图。 */
typedef struct {
    int8_t *data; /**< 数据缓冲。 */
    uint32_t byte_count; /**< 数据字节数。 */
    uint32_t dims[4]; /**< 维度值。 */
    uint32_t ndim; /**< 有效维数。 */
} bm_tflm_tensor_view_t;

/**
 * @brief 查询 TinyML 算子对应的 TFLM BuiltinOperator ID。
 * @param bm_op 待查询的 TinyML 算子。
 * @param map 自定义映射表；为 NULL 时使用默认映射。
 * @param map_count 自定义映射项数；map 为 NULL 时忽略。
 * @return 找到时返回 BuiltinOperator ID 载荷；未找到时返回 -1 载荷哨兵，
 *         -1 不是框架状态码。
 */
static inline int bm_tflm_bridge_lookup_op(bm_tinyml_op_t bm_op,
                                           const bm_tflm_op_map_t *map,
                                           uint32_t map_count) {
    static const bm_tflm_op_map_t default_map[] = {
        { BM_TINYML_OP_QUANTIZE, BM_TFLM_BUILTIN_QUANTIZE },
        { BM_TINYML_OP_FC, BM_TFLM_BUILTIN_FULLY_CONNECTED },
        { BM_TINYML_OP_DEQUANTIZE, BM_TFLM_BUILTIN_DEQUANTIZE },
        { BM_TINYML_OP_RELU, BM_TFLM_BUILTIN_RELU },
        { BM_TINYML_OP_SOFTMAX, BM_TFLM_BUILTIN_SOFTMAX },
        { BM_TINYML_OP_FLATTEN, BM_TFLM_BUILTIN_RESHAPE },
        { BM_TINYML_OP_ADD, BM_TFLM_BUILTIN_ADD },
        { BM_TINYML_OP_MUL, BM_TFLM_BUILTIN_MUL },
        { BM_TINYML_OP_MAXPOOL_2X2, BM_TFLM_BUILTIN_POOL_2D },
        { BM_TINYML_OP_DEPTHWISE_CONV2D, BM_TFLM_BUILTIN_DEPTHWISE_CONV_2D },
        { BM_TINYML_OP_CONV2D_1X1, BM_TFLM_BUILTIN_CONV_2D }
    };
    const bm_tflm_op_map_t *table = (map != NULL) ? map : default_map;
    uint32_t count = (map != NULL) ? map_count :
        (uint32_t)(sizeof(default_map) / sizeof(default_map[0]));
    uint32_t i;

    for (i = 0u; i < count; ++i) {
        if (table[i].bm_op == bm_op) {
            return table[i].tflm_builtin_id;
        }
    }
    return -1;
}

/**
 * @brief 从 TinyML 图导出 TFLM bridge 配置。
 * @param graph TinyML 图；为 NULL 时静默返回。
 * @param out 输出 bridge 配置；为 NULL 时静默返回。
 * @return 无返回值。
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
 * @brief 导出单个张量的轻量只读视图。
 * @param src 源张量；为 NULL 时静默返回。
 * @param dst 输出视图；为 NULL 时静默返回。
 * @return 无返回值。
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

#ifdef __cplusplus
}
#endif

#endif /* BM_TINYML_CONTRACT_H */
