/**
 * @file test_tinyml_adapter.c
 * @brief tinyml_adapter 组件单元测试
 *
 * 覆盖静态 arena 分配、i8 tensor 量化往返与最小算子图。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            正式发布
 * 2026-06-17       1.1            zeh            最小算子图 quantize/fc
 * 2026-06-17       1.2            zeh            ReLU 节点测试
 * 2026-06-17       1.3            zeh            SOFTMAX/FLATTEN 链测试
 * 2026-06-17       1.4            zeh            ADD 双输入链测试
 * 2026-06-17       1.5            zeh            MUL 双输入链测试
 * 2026-06-17       1.6            zeh            CONV2D 1x1 NCHW 测试
 * 2026-06-17       1.7            zeh            tflm_runtime stub 空回调注册测试
 */
#include "unity.h"
#include "bm/component/tinyml_adapter.h"
#include "bm/component/tinyml_tflm_bridge.h"
#include "bm/component/tinyml_tflm_runtime.h"
#include "bm/common/bm_types.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_tinyml_arena_tensor_quantize(void) {
    bm_tinyml_arena_t arena;
    bm_tinyml_tensor_t tensor;
    uint32_t dims[2] = { 4u, 4u };
    bm_tinyml_quant_params_t quant = { .scale = 0.1f, .zero_point = 0 };
    float src[16];
    float dst[16];
    uint32_t i;

    for (i = 0u; i < 16u; ++i) {
        src[i] = (float)i * 0.05f;
    }

    bm_tinyml_arena_reset(&arena);
    TEST_ASSERT_EQUAL(0, bm_tinyml_tensor_alloc_i8(&arena, &tensor,
                                                   dims, 2u, &quant));
    TEST_ASSERT_EQUAL(0, bm_tinyml_tensor_quantize_f32(&tensor, src, 16u));
    TEST_ASSERT_EQUAL(0, bm_tinyml_tensor_dequantize_f32(&tensor, dst, 16u));
    TEST_ASSERT_FLOAT_WITHIN(0.15f, src[5], dst[5]);
    TEST_ASSERT_TRUE(bm_tinyml_arena_bytes_used(&arena) >= 16u);
}

void test_tinyml_graph_quantize_fc_run(void) {
    bm_tinyml_arena_t arena;
    bm_tinyml_tensor_t tensors[2];
    uint32_t in_dims[1] = { 4u };
    uint32_t out_dims[1] = { 2u };
    bm_tinyml_quant_params_t quant = { .scale = 0.5f, .zero_point = 0 };
    static const int8_t weights[8] = {
        127, 0, 0, 0,
        0, 127, 0, 0
    };
    bm_tinyml_graph_node_t nodes[3];
    bm_tinyml_graph_t graph;
    float inputs[4] = { 1.0f, 0.5f, 0.0f, 0.0f };
    float outputs[2];

    bm_tinyml_arena_reset(&arena);
    memset(tensors, 0, sizeof(tensors));
    TEST_ASSERT_EQUAL(0, bm_tinyml_tensor_alloc_i8(&arena, &tensors[0],
                                                   in_dims, 1u, &quant));
    TEST_ASSERT_EQUAL(0, bm_tinyml_tensor_alloc_i8(&arena, &tensors[1],
                                                   out_dims, 1u, &quant));

    nodes[0].op = BM_TINYML_OP_QUANTIZE;
    nodes[0].input_tensor = 0u;
    nodes[0].input_tensor_b = 0u;
    nodes[0].output_tensor = 0u;
    nodes[0].fc_weights = NULL;
    nodes[0].fc_in_dim = 0u;
    nodes[0].fc_out_dim = 0u;

    nodes[1].op = BM_TINYML_OP_FC;
    nodes[1].input_tensor = 0u;
    nodes[1].input_tensor_b = 0u;
    nodes[1].output_tensor = 1u;
    nodes[1].fc_weights = weights;
    nodes[1].fc_in_dim = 4u;
    nodes[1].fc_out_dim = 2u;

    nodes[2].op = BM_TINYML_OP_DEQUANTIZE;
    nodes[2].input_tensor = 1u;
    nodes[2].input_tensor_b = 0u;
    nodes[2].output_tensor = 1u;
    nodes[2].fc_weights = NULL;
    nodes[2].fc_in_dim = 0u;
    nodes[2].fc_out_dim = 0u;

    graph.nodes = nodes;
    graph.node_count = 3u;
    graph.arena = &arena;
    graph.tensors = tensors;
    graph.tensor_count = 2u;

    TEST_ASSERT_EQUAL(0, bm_tinyml_graph_init(&graph));
    TEST_ASSERT_EQUAL(0, bm_tinyml_graph_run(&graph, inputs, 4u,
                                             outputs, 2u));
    TEST_ASSERT_FLOAT_WITHIN(0.6f, 1.0f, outputs[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.6f, 0.5f, outputs[1]);
}

void test_tinyml_graph_relu_node(void) {
    bm_tinyml_arena_t arena;
    bm_tinyml_tensor_t tensors[1];
    uint32_t dims[1] = { 4u };
    bm_tinyml_quant_params_t quant = { .scale = 1.0f, .zero_point = 0 };
    bm_tinyml_graph_node_t nodes[1];
    bm_tinyml_graph_t graph;

    bm_tinyml_arena_reset(&arena);
    TEST_ASSERT_EQUAL(0, bm_tinyml_tensor_alloc_i8(&arena, &tensors[0],
                                                   dims, 1u, &quant));
    tensors[0].data[0] = (int8_t)-5;
    tensors[0].data[1] = (int8_t)3;
    tensors[0].data[2] = (int8_t)-1;
    tensors[0].data[3] = (int8_t)7;

    nodes[0].op = BM_TINYML_OP_RELU;
    nodes[0].input_tensor = 0u;
    nodes[0].input_tensor_b = 0u;
    nodes[0].output_tensor = 0u;
    nodes[0].fc_weights = NULL;
    nodes[0].fc_in_dim = 0u;
    nodes[0].fc_out_dim = 0u;

    graph.nodes = nodes;
    graph.node_count = 1u;
    graph.arena = &arena;
    graph.tensors = tensors;
    graph.tensor_count = 1u;

    TEST_ASSERT_EQUAL(0, bm_tinyml_graph_run(&graph, NULL, 0u, NULL, 0u));
    TEST_ASSERT_EQUAL_INT8(0, tensors[0].data[0]);
    TEST_ASSERT_EQUAL_INT8(3, tensors[0].data[1]);
    TEST_ASSERT_EQUAL_INT8(0, tensors[0].data[2]);
    TEST_ASSERT_EQUAL_INT8(7, tensors[0].data[3]);
}

void test_tinyml_graph_softmax_flatten_chain(void) {
    bm_tinyml_arena_t arena;
    bm_tinyml_tensor_t tensors[2];
    uint32_t in_dims[2] = { 2u, 2u };
    uint32_t flat_dims[2] = { 1u, 4u };
    bm_tinyml_quant_params_t quant = { .scale = 1.0f, .zero_point = 0 };
    bm_tinyml_graph_node_t nodes[2];
    bm_tinyml_graph_t graph;

    bm_tinyml_arena_reset(&arena);
    TEST_ASSERT_EQUAL(0, bm_tinyml_tensor_alloc_i8(&arena, &tensors[0],
                                                   in_dims, 2u, &quant));
    TEST_ASSERT_EQUAL(0, bm_tinyml_tensor_alloc_i8(&arena, &tensors[1],
                                                   flat_dims, 2u, &quant));
    tensors[0].data[0] = (int8_t)1;
    tensors[0].data[1] = (int8_t)2;
    tensors[0].data[2] = (int8_t)3;
    tensors[0].data[3] = (int8_t)4;

    nodes[0].op = BM_TINYML_OP_SOFTMAX;
    nodes[0].input_tensor = 0u;
    nodes[0].input_tensor_b = 0u;
    nodes[0].output_tensor = 0u;
    nodes[0].fc_weights = NULL;
    nodes[0].fc_in_dim = 0u;
    nodes[0].fc_out_dim = 0u;

    nodes[1].op = BM_TINYML_OP_FLATTEN;
    nodes[1].input_tensor = 0u;
    nodes[1].input_tensor_b = 0u;
    nodes[1].output_tensor = 1u;
    nodes[1].fc_weights = NULL;
    nodes[1].fc_in_dim = 0u;
    nodes[1].fc_out_dim = 0u;

    graph.nodes = nodes;
    graph.node_count = 2u;
    graph.arena = &arena;
    graph.tensors = tensors;
    graph.tensor_count = 2u;

    TEST_ASSERT_EQUAL(0, bm_tinyml_graph_run(&graph, NULL, 0u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT32(2u, tensors[1].ndim);
    TEST_ASSERT_EQUAL_UINT32(1u, tensors[1].dims[0]);
    TEST_ASSERT_EQUAL_UINT32(4u, tensors[1].dims[1]);
    TEST_ASSERT_EQUAL_PTR(tensors[0].data, tensors[1].data);
    TEST_ASSERT_TRUE(tensors[0].data[3] > tensors[0].data[0]);
    {
        int32_t sum = 0;
        uint32_t i;
        for (i = 0u; i < 4u; ++i) {
            sum += (int32_t)tensors[0].data[i];
        }
        TEST_ASSERT_TRUE(sum > 100);
    }
}

void test_tinyml_graph_add_node(void) {
    bm_tinyml_arena_t arena;
    bm_tinyml_tensor_t tensors[3];
    uint32_t dims[1] = { 4u };
    bm_tinyml_quant_params_t quant = { .scale = 1.0f, .zero_point = 0 };
    bm_tinyml_graph_node_t nodes[1];
    bm_tinyml_graph_t graph;

    bm_tinyml_arena_reset(&arena);
    TEST_ASSERT_EQUAL(0, bm_tinyml_tensor_alloc_i8(&arena, &tensors[0],
                                                   dims, 1u, &quant));
    TEST_ASSERT_EQUAL(0, bm_tinyml_tensor_alloc_i8(&arena, &tensors[1],
                                                   dims, 1u, &quant));
    TEST_ASSERT_EQUAL(0, bm_tinyml_tensor_alloc_i8(&arena, &tensors[2],
                                                   dims, 1u, &quant));

    tensors[0].data[0] = (int8_t)10;
    tensors[0].data[1] = (int8_t)-5;
    tensors[0].data[2] = (int8_t)100;
    tensors[0].data[3] = (int8_t)120;
    tensors[1].data[0] = (int8_t)5;
    tensors[1].data[1] = (int8_t)10;
    tensors[1].data[2] = (int8_t)-50;
    tensors[1].data[3] = (int8_t)10;

    nodes[0].op = BM_TINYML_OP_ADD;
    nodes[0].input_tensor = 0u;
    nodes[0].input_tensor_b = 1u;
    nodes[0].output_tensor = 2u;
    nodes[0].fc_weights = NULL;
    nodes[0].fc_in_dim = 0u;
    nodes[0].fc_out_dim = 0u;

    graph.nodes = nodes;
    graph.node_count = 1u;
    graph.arena = &arena;
    graph.tensors = tensors;
    graph.tensor_count = 3u;

    TEST_ASSERT_EQUAL(0, bm_tinyml_graph_run(&graph, NULL, 0u, NULL, 0u));
    TEST_ASSERT_EQUAL_INT8(15, tensors[2].data[0]);
    TEST_ASSERT_EQUAL_INT8(5, tensors[2].data[1]);
    TEST_ASSERT_EQUAL_INT8(50, tensors[2].data[2]);
    TEST_ASSERT_EQUAL_INT8(127, tensors[2].data[3]);
}

void test_tinyml_graph_mul_node(void) {
    bm_tinyml_arena_t arena;
    bm_tinyml_tensor_t tensors[3];
    uint32_t dims[1] = { 4u };
    bm_tinyml_quant_params_t quant = { .scale = 1.0f, .zero_point = 0 };
    bm_tinyml_graph_node_t nodes[1];
    bm_tinyml_graph_t graph;

    bm_tinyml_arena_reset(&arena);
    TEST_ASSERT_EQUAL(0, bm_tinyml_tensor_alloc_i8(&arena, &tensors[0],
                                                   dims, 1u, &quant));
    TEST_ASSERT_EQUAL(0, bm_tinyml_tensor_alloc_i8(&arena, &tensors[1],
                                                   dims, 1u, &quant));
    TEST_ASSERT_EQUAL(0, bm_tinyml_tensor_alloc_i8(&arena, &tensors[2],
                                                   dims, 1u, &quant));

    tensors[0].data[0] = (int8_t)4;
    tensors[0].data[1] = (int8_t)-2;
    tensors[0].data[2] = (int8_t)8;
    tensors[0].data[3] = (int8_t)127;
    tensors[1].data[0] = (int8_t)2;
    tensors[1].data[1] = (int8_t)4;
    tensors[1].data[2] = (int8_t)-4;
    tensors[1].data[3] = (int8_t)2;

    nodes[0].op = BM_TINYML_OP_MUL;
    nodes[0].input_tensor = 0u;
    nodes[0].input_tensor_b = 1u;
    nodes[0].output_tensor = 2u;
    nodes[0].fc_weights = NULL;
    nodes[0].fc_in_dim = 0u;
    nodes[0].fc_out_dim = 0u;

    graph.nodes = nodes;
    graph.node_count = 1u;
    graph.arena = &arena;
    graph.tensors = tensors;
    graph.tensor_count = 3u;

    TEST_ASSERT_EQUAL(0, bm_tinyml_graph_run(&graph, NULL, 0u, NULL, 0u));
    TEST_ASSERT_EQUAL_INT8(0, tensors[2].data[0]);
    TEST_ASSERT_EQUAL_INT8(-1, tensors[2].data[1]);
    TEST_ASSERT_EQUAL_INT8(-1, tensors[2].data[2]);
    TEST_ASSERT_EQUAL_INT8(1, tensors[2].data[3]);
}

void test_tinyml_graph_maxpool_2x2_node(void) {
    bm_tinyml_arena_t arena;
    bm_tinyml_tensor_t tensors[2];
    uint32_t in_dims[2] = { 4u, 4u };
    uint32_t out_dims[2] = { 2u, 2u };
    bm_tinyml_quant_params_t quant = { .scale = 1.0f, .zero_point = 0 };
    bm_tinyml_graph_node_t nodes[1];
    bm_tinyml_graph_t graph;
    static const int8_t expected[4] = { 8, 7, 11, 14 };

    bm_tinyml_arena_reset(&arena);
    TEST_ASSERT_EQUAL(0, bm_tinyml_tensor_alloc_i8(&arena, &tensors[0],
                                                   in_dims, 2u, &quant));
    TEST_ASSERT_EQUAL(0, bm_tinyml_tensor_alloc_i8(&arena, &tensors[1],
                                                   out_dims, 2u, &quant));

    tensors[0].data[0] = (int8_t)1;
    tensors[0].data[1] = (int8_t)3;
    tensors[0].data[2] = (int8_t)2;
    tensors[0].data[3] = (int8_t)6;
    tensors[0].data[4] = (int8_t)5;
    tensors[0].data[5] = (int8_t)8;
    tensors[0].data[6] = (int8_t)4;
    tensors[0].data[7] = (int8_t)7;
    tensors[0].data[8] = (int8_t)9;
    tensors[0].data[9] = (int8_t)11;
    tensors[0].data[10] = (int8_t)12;
    tensors[0].data[11] = (int8_t)10;
    tensors[0].data[12] = (int8_t)0;
    tensors[0].data[13] = (int8_t)6;
    tensors[0].data[14] = (int8_t)14;
    tensors[0].data[15] = (int8_t)13;

    nodes[0].op = BM_TINYML_OP_MAXPOOL_2X2;
    nodes[0].input_tensor = 0u;
    nodes[0].input_tensor_b = 0u;
    nodes[0].output_tensor = 1u;
    nodes[0].fc_weights = NULL;
    nodes[0].fc_in_dim = 0u;
    nodes[0].fc_out_dim = 0u;

    graph.nodes = nodes;
    graph.node_count = 1u;
    graph.arena = &arena;
    graph.tensors = tensors;
    graph.tensor_count = 2u;

    TEST_ASSERT_EQUAL(0, bm_tinyml_graph_run(&graph, NULL, 0u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT32(2u, tensors[1].dims[0]);
    TEST_ASSERT_EQUAL_UINT32(2u, tensors[1].dims[1]);
    TEST_ASSERT_EQUAL_INT8(expected[0], tensors[1].data[0]);
    TEST_ASSERT_EQUAL_INT8(expected[1], tensors[1].data[1]);
    TEST_ASSERT_EQUAL_INT8(expected[2], tensors[1].data[2]);
    TEST_ASSERT_EQUAL_INT8(expected[3], tensors[1].data[3]);
}

static void test_tinyml_graph_depthwise_conv2d_node(void) {
    bm_tinyml_arena_t arena;
    bm_tinyml_tensor_t tensors[2];
    uint32_t in_dims[2] = { 3u, 3u };
    uint32_t out_dims[2] = { 1u, 1u };
    bm_tinyml_quant_params_t quant = { .scale = 1.0f, .zero_point = 0 };
    bm_tinyml_graph_node_t nodes[1];
    bm_tinyml_graph_t graph;
    static const int8_t kernel[9] = {
        0, 0, 0,
        0, 127, 0,
        0, 0, 0
    };
    uint32_t i;

    bm_tinyml_arena_reset(&arena);
    TEST_ASSERT_EQUAL(0, bm_tinyml_tensor_alloc_i8(&arena, &tensors[0],
                                                   in_dims, 2u, &quant));
    TEST_ASSERT_EQUAL(0, bm_tinyml_tensor_alloc_i8(&arena, &tensors[1],
                                                   out_dims, 2u, &quant));

    for (i = 0u; i < 9u; ++i) {
        tensors[0].data[i] = 0;
    }
    tensors[0].data[4] = (int8_t)10;

    nodes[0].op = BM_TINYML_OP_DEPTHWISE_CONV2D;
    nodes[0].input_tensor = 0u;
    nodes[0].input_tensor_b = 0u;
    nodes[0].output_tensor = 1u;
    nodes[0].fc_weights = kernel;
    nodes[0].fc_in_dim = 9u;
    nodes[0].fc_out_dim = 1u;

    graph.nodes = nodes;
    graph.node_count = 1u;
    graph.arena = &arena;
    graph.tensors = tensors;
    graph.tensor_count = 2u;

    TEST_ASSERT_EQUAL(0, bm_tinyml_graph_run(&graph, NULL, 0u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT32(1u, tensors[1].dims[0]);
    TEST_ASSERT_EQUAL_UINT32(1u, tensors[1].dims[1]);
    TEST_ASSERT_EQUAL_INT8(9, tensors[1].data[0]);
}

static void test_tinyml_graph_conv2d_1x1_node(void) {
    bm_tinyml_arena_t arena;
    bm_tinyml_tensor_t tensors[2];
    uint32_t in_dims[4] = { 1u, 2u, 2u, 2u };
    uint32_t out_dims[4] = { 1u, 2u, 2u, 2u };
    bm_tinyml_quant_params_t quant = { .scale = 1.0f, .zero_point = 0 };
    bm_tinyml_graph_node_t nodes[1];
    bm_tinyml_graph_t graph;
    static const int8_t weights[4] = {
        127, 0,
        0, 127
    };

    bm_tinyml_arena_reset(&arena);
    TEST_ASSERT_EQUAL(0, bm_tinyml_tensor_alloc_i8(&arena, &tensors[0],
                                                   in_dims, 4u, &quant));
    TEST_ASSERT_EQUAL(0, bm_tinyml_tensor_alloc_i8(&arena, &tensors[1],
                                                   out_dims, 4u, &quant));

    tensors[0].data[0] = (int8_t)2;
    tensors[0].data[1] = (int8_t)0;
    tensors[0].data[2] = (int8_t)0;
    tensors[0].data[3] = (int8_t)4;
    tensors[0].data[4] = (int8_t)1;
    tensors[0].data[5] = (int8_t)0;
    tensors[0].data[6] = (int8_t)0;
    tensors[0].data[7] = (int8_t)3;

    nodes[0].op = BM_TINYML_OP_CONV2D_1X1;
    nodes[0].input_tensor = 0u;
    nodes[0].input_tensor_b = 0u;
    nodes[0].output_tensor = 1u;
    nodes[0].fc_weights = weights;
    nodes[0].fc_in_dim = 2u;
    nodes[0].fc_out_dim = 2u;

    graph.nodes = nodes;
    graph.node_count = 1u;
    graph.arena = &arena;
    graph.tensors = tensors;
    graph.tensor_count = 2u;

    TEST_ASSERT_EQUAL(0, bm_tinyml_graph_run(&graph, NULL, 0u, NULL, 0u));
    TEST_ASSERT_EQUAL_INT8(1, tensors[1].data[0]);
    TEST_ASSERT_EQUAL_INT8(2, tensors[1].data[7]);
    TEST_ASSERT_EQUAL(3, bm_tflm_bridge_lookup_op(BM_TINYML_OP_CONV2D_1X1,
                                                  NULL, 0u));
}

void test_tinyml_tflm_runtime_stub_register_invoke(void) {
    bm_tinyml_tflm_runtime_t runtime;

    TEST_ASSERT_EQUAL(BM_OK, bm_tinyml_tflm_register_ops(NULL));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_SUPPORTED, bm_tinyml_tflm_invoke(&runtime));
    TEST_ASSERT_EQUAL(BM_OK, bm_tinyml_tflm_runtime_init(&runtime));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_tinyml_arena_tensor_quantize);
    RUN_TEST(test_tinyml_graph_quantize_fc_run);
    RUN_TEST(test_tinyml_graph_relu_node);
    RUN_TEST(test_tinyml_graph_softmax_flatten_chain);
    RUN_TEST(test_tinyml_graph_add_node);
    RUN_TEST(test_tinyml_graph_mul_node);
    RUN_TEST(test_tinyml_graph_maxpool_2x2_node);
    RUN_TEST(test_tinyml_graph_depthwise_conv2d_node);
    RUN_TEST(test_tinyml_graph_conv2d_1x1_node);
    RUN_TEST(test_tinyml_tflm_runtime_stub_register_invoke);
    return UNITY_END();
}
