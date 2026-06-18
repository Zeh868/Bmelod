/**
 * @file main.c
 * @brief TinyML 最小算子图示例：quantize → depthwise 3×3 → dequantize
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            初始 native_sim 图推理
 * 2026-06-17       1.1            zeh            插入 DEPTHWISE_CONV2D 节点演示
 *
 */
#include "bm/component/tinyml_adapter.h"
#include "hybrid_print.h"

#include <stdio.h>
#include <string.h>

#define GRAPH_H        3u
#define GRAPH_W        3u
#define GRAPH_IN_COUNT (GRAPH_H * GRAPH_W)

static bm_tinyml_arena_t g_arena;
static bm_tinyml_tensor_t g_tensors[2];
static bm_tinyml_graph_node_t g_nodes[3];
static bm_tinyml_graph_t g_graph;

/** 3×3 中心权重核（int8，>>7 定点乘） */
static const int8_t g_dw_kernel[9] = {
    0, 0, 0,
    0, 127, 0,
    0, 0, 0
};

static int build_graph(void) {
    uint32_t in_dims[2] = { GRAPH_H, GRAPH_W };
    uint32_t out_dims[2] = { 1u, 1u };
    bm_tinyml_quant_params_t quant = { .scale = 1.0f, .zero_point = 0 };

    bm_tinyml_arena_reset(&g_arena);
    memset(g_tensors, 0, sizeof(g_tensors));

    if (bm_tinyml_tensor_alloc_i8(&g_arena, &g_tensors[0], in_dims, 2u,
                                  &quant) != 0) {
        return -1;
    }
    if (bm_tinyml_tensor_alloc_i8(&g_arena, &g_tensors[1], out_dims, 2u,
                                  &quant) != 0) {
        return -1;
    }

    g_nodes[0].op = BM_TINYML_OP_QUANTIZE;
    g_nodes[0].input_tensor = 0u;
    g_nodes[0].input_tensor_b = 0u;
    g_nodes[0].output_tensor = 0u;

    g_nodes[1].op = BM_TINYML_OP_DEPTHWISE_CONV2D;
    g_nodes[1].input_tensor = 0u;
    g_nodes[1].input_tensor_b = 0u;
    g_nodes[1].output_tensor = 1u;
    g_nodes[1].fc_weights = g_dw_kernel;
    g_nodes[1].fc_in_dim = 9u;
    g_nodes[1].fc_out_dim = 1u;

    g_nodes[2].op = BM_TINYML_OP_DEQUANTIZE;
    g_nodes[2].input_tensor = 1u;
    g_nodes[2].input_tensor_b = 0u;
    g_nodes[2].output_tensor = 1u;

    g_graph.nodes = g_nodes;
    g_graph.node_count = 3u;
    g_graph.arena = &g_arena;
    g_graph.tensors = g_tensors;
    g_graph.tensor_count = 2u;

    return bm_tinyml_graph_init(&g_graph);
}

int main(void) {
    float inputs[GRAPH_IN_COUNT] = {
        0.0f, 0.0f, 0.0f,
        0.0f, 10.0f, 0.0f,
        0.0f, 0.0f, 0.0f
    };
    float outputs[1];
    char line[96];

    hybrid_print("Bmelod Example: tinyml_graph (depthwise 3x3)\n");

    if (build_graph() != 0) {
        hybrid_print("EXAMPLE_TINYML_GRAPH: FAIL init\n");
        return 1;
    }

    if (bm_tinyml_graph_run(&g_graph, inputs, GRAPH_IN_COUNT,
                            outputs, 1u) != 0) {
        hybrid_print("EXAMPLE_TINYML_GRAPH: FAIL run\n");
        return 1;
    }

    (void)snprintf(line, sizeof(line), "dequant[0]=%.4f\n",
                   (double)outputs[0]);
    hybrid_print(line);

    (void)snprintf(line, sizeof(line), "arena_peak=%u\n",
                   (unsigned)g_arena.peak_bytes);
    hybrid_print(line);

    if (outputs[0] <= 0.0f) {
        hybrid_print("EXAMPLE_TINYML_GRAPH: FAIL output\n");
        return 1;
    }

    hybrid_print_pass("TINYML_GRAPH");
    return 0;
}
