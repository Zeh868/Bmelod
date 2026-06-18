/**
 * @file test_pipeline.c
 * @brief bm_pipeline 线性链单元测试
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 */
#include "unity.h"
#include "bm_pipeline.h"
#include "bm_block.h"

#include <string.h>

#define TEST_FMT_A  0xA001u
#define TEST_FMT_B  0xA002u

typedef struct {
    uint32_t value;
} test_payload_t;

typedef struct {
    uint32_t addend;
} bump_cfg_t;

typedef struct {
    uint32_t calls;
} bump_state_t;

static int bump_prepare(void *state, const void *config) {
    bump_state_t *st = (bump_state_t *)state;

    (void)config;
    if (st == NULL) {
        return BM_ERR_INVALID;
    }
    st->calls = 0u;
    return BM_OK;
}

static bump_cfg_t s_bump_cfg_a = { .addend = 1u };
static bump_cfg_t s_bump_cfg_b = { .addend = 10u };
static bump_state_t s_bump_state_a;
static bump_state_t s_bump_state_b;

static int bump_a_process(void *state,
                          const bm_block_t *input,
                          bm_block_t *output) {
    bump_state_t *st = (bump_state_t *)state;
    test_payload_t *out_payload;

    if (st == NULL || input == NULL || output == NULL ||
        input->data == NULL || output->data == NULL) {
        return BM_ERR_INVALID;
    }

    out_payload = (test_payload_t *)output->data;
    out_payload->value = ((const test_payload_t *)input->data)->value +
                         s_bump_cfg_a.addend;
    st->calls++;
    return BM_OK;
}

static int bump_b_process(void *state,
                          const bm_block_t *input,
                          bm_block_t *output) {
    bump_state_t *st = (bump_state_t *)state;
    test_payload_t *out_payload;

    if (st == NULL || input == NULL || output == NULL ||
        input->data == NULL || output->data == NULL) {
        return BM_ERR_INVALID;
    }

    out_payload = (test_payload_t *)output->data;
    out_payload->value = ((const test_payload_t *)input->data)->value +
                         s_bump_cfg_b.addend;
    st->calls++;
    return BM_OK;
}

static const bm_pipeline_node_ops_t s_bump_ops_a = {
    bump_prepare, bump_a_process, NULL, "bump_a"
};

static const bm_pipeline_node_ops_t s_bump_ops_b = {
    bump_prepare, bump_b_process, NULL, "bump_b"
};

static const bm_pipeline_node_ops_t s_no_process_ops = {
    bump_prepare, NULL, NULL, "no_process"
};

static bm_pipeline_node_t s_nodes[2];
static bm_pipeline_t s_pipeline;
static test_payload_t s_payload;
static test_payload_t s_out_payload;
static bm_block_t s_block;
static bm_block_t s_out_block;

void setUp(void) {
    memset(&s_pipeline, 0, sizeof(s_pipeline));
    memset(&s_bump_state_a, 0, sizeof(s_bump_state_a));
    memset(&s_bump_state_b, 0, sizeof(s_bump_state_b));
    memset(&s_payload, 0, sizeof(s_payload));
    memset(&s_out_payload, 0, sizeof(s_out_payload));
    memset(&s_block, 0, sizeof(s_block));
    memset(&s_out_block, 0, sizeof(s_out_block));

    s_block.data = &s_payload;
    s_block.capacity_bytes = sizeof(s_payload);
    s_block.valid_bytes = sizeof(s_payload);
    s_block.sequence = 42u;
    s_block.timestamp.ticks = 1234u;
    s_block.timestamp.rate_hz = 1000u;
    s_block.timestamp.clock_id = 2u;
    s_block.timestamp.quality = 3u;
    s_block.format = TEST_FMT_A;
    s_block.flags = 0x55AAu;

    s_out_block.data = &s_out_payload;
    s_out_block.capacity_bytes = sizeof(s_out_payload);
    s_out_block.valid_bytes = 0u;
    s_out_block.format = 0u;

    s_nodes[0].ops = &s_bump_ops_a;
    s_nodes[0].state = &s_bump_state_a;
    s_nodes[0].config = &s_bump_cfg_a;
    s_nodes[0].input_format = TEST_FMT_A;
    s_nodes[0].output_format = TEST_FMT_B;
    s_nodes[0].bypass = 0u;

    s_nodes[1].ops = &s_bump_ops_b;
    s_nodes[1].state = &s_bump_state_b;
    s_nodes[1].config = &s_bump_cfg_b;
    s_nodes[1].input_format = TEST_FMT_B;
    s_nodes[1].output_format = TEST_FMT_B;
    s_nodes[1].bypass = 0u;
}

void tearDown(void) {
}

void test_pipeline_inplace_chain(void) {
    s_payload.value = 5u;

    TEST_ASSERT_EQUAL(BM_OK, bm_pipeline_init(&s_pipeline, s_nodes, 2u));
    TEST_ASSERT_EQUAL(BM_OK, bm_pipeline_process_inplace(&s_pipeline, &s_block));
    TEST_ASSERT_EQUAL(16u, s_payload.value);
    TEST_ASSERT_EQUAL(1u, s_bump_state_a.calls);
    TEST_ASSERT_EQUAL(1u, s_bump_state_b.calls);
    TEST_ASSERT_EQUAL(TEST_FMT_B, s_block.format);
}

void test_pipeline_bypass_skips_node(void) {
    s_payload.value = 3u;
    s_nodes[1].bypass = 1u;

    TEST_ASSERT_EQUAL(BM_OK, bm_pipeline_init(&s_pipeline, s_nodes, 2u));
    TEST_ASSERT_EQUAL(BM_OK, bm_pipeline_process_inplace(&s_pipeline, &s_block));
    TEST_ASSERT_EQUAL(4u, s_payload.value);
    TEST_ASSERT_EQUAL(1u, s_bump_state_a.calls);
    TEST_ASSERT_EQUAL(0u, s_bump_state_b.calls);
}

void test_pipeline_format_mismatch_rejected(void) {
    bm_pipeline_node_t bad_nodes[2];

    memcpy(bad_nodes, s_nodes, sizeof(bad_nodes));
    bad_nodes[1].input_format = 0xFFFFu;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_pipeline_init(&s_pipeline, bad_nodes, 2u));
}

void test_pipeline_separate_io_chain(void) {
    s_payload.value = 7u;
    s_out_payload.value = 0xDEADu;

    TEST_ASSERT_EQUAL(BM_OK, bm_pipeline_init(&s_pipeline, s_nodes, 2u));
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_pipeline_process(&s_pipeline, &s_block, &s_out_block));
    TEST_ASSERT_EQUAL(18u, s_out_payload.value);
    TEST_ASSERT_EQUAL(sizeof(s_payload), s_out_block.valid_bytes);
    TEST_ASSERT_EQUAL(s_block.sequence, s_out_block.sequence);
    TEST_ASSERT_EQUAL(s_block.timestamp.ticks, s_out_block.timestamp.ticks);
    TEST_ASSERT_EQUAL(s_block.timestamp.rate_hz,
                      s_out_block.timestamp.rate_hz);
    TEST_ASSERT_EQUAL(s_block.timestamp.clock_id,
                      s_out_block.timestamp.clock_id);
    TEST_ASSERT_EQUAL(s_block.timestamp.quality,
                      s_out_block.timestamp.quality);
    TEST_ASSERT_EQUAL(TEST_FMT_B, s_out_block.format);
    TEST_ASSERT_EQUAL(s_block.flags, s_out_block.flags);
    TEST_ASSERT_EQUAL(1u, s_bump_state_a.calls);
    TEST_ASSERT_EQUAL(1u, s_bump_state_b.calls);
}

void test_pipeline_bypass_first_node_separate_io(void) {
    bm_pipeline_node_t nodes[2];

    memcpy(nodes, s_nodes, sizeof(nodes));
    nodes[0].input_format = TEST_FMT_B;
    nodes[0].output_format = TEST_FMT_B;
    nodes[0].bypass = 1u;

    s_payload.value = 9u;
    s_out_payload.value = 0xDEADu;
    s_block.format = TEST_FMT_B;

    TEST_ASSERT_EQUAL(BM_OK, bm_pipeline_init(&s_pipeline, nodes, 2u));
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_pipeline_process(&s_pipeline, &s_block, &s_out_block));
    TEST_ASSERT_EQUAL(19u, s_out_payload.value);
    TEST_ASSERT_EQUAL(TEST_FMT_B, s_out_block.format);
    TEST_ASSERT_EQUAL(0u, s_bump_state_a.calls);
    TEST_ASSERT_EQUAL(1u, s_bump_state_b.calls);
}

void test_pipeline_bypass_format_converter_rejected(void) {
    bm_pipeline_node_t nodes[2];

    memcpy(nodes, s_nodes, sizeof(nodes));
    nodes[0].bypass = 1u;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_pipeline_init(&s_pipeline, nodes, 2u));
}

void test_pipeline_set_bypass_format_converter_rejected(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_pipeline_init(&s_pipeline, s_nodes, 2u));
    TEST_ASSERT_EQUAL(BM_ERR_BUSY,
                      bm_pipeline_set_bypass(&s_pipeline, 0u, 1));
}

void test_pipeline_set_bypass_rejected_after_init(void) {
    s_nodes[1].input_format = TEST_FMT_B;
    s_nodes[1].output_format = TEST_FMT_B;

    TEST_ASSERT_EQUAL(BM_OK, bm_pipeline_init(&s_pipeline, s_nodes, 2u));
    TEST_ASSERT_EQUAL(BM_ERR_BUSY,
                      bm_pipeline_set_bypass(&s_pipeline, 1u, 1));
}

void test_pipeline_bypass_rejects_small_output(void) {
    bm_pipeline_node_t node;

    node = s_nodes[1];
    node.bypass = 1u;
    s_block.format = TEST_FMT_B;
    s_out_block.capacity_bytes = sizeof(s_payload) - 1u;

    TEST_ASSERT_EQUAL(BM_OK, bm_pipeline_init(&s_pipeline, &node, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_OVERFLOW,
                      bm_pipeline_process(&s_pipeline,
                                          &s_block,
                                          &s_out_block));
    TEST_ASSERT_EQUAL(0u, s_out_block.valid_bytes);
}

void test_pipeline_process_rejects_small_output(void) {
    s_out_block.capacity_bytes = sizeof(s_payload) - 1u;

    TEST_ASSERT_EQUAL(BM_OK, bm_pipeline_init(&s_pipeline, s_nodes, 2u));
    TEST_ASSERT_EQUAL(BM_ERR_OVERFLOW,
                      bm_pipeline_process(&s_pipeline,
                                          &s_block,
                                          &s_out_block));
    TEST_ASSERT_EQUAL(0u, s_out_block.valid_bytes);
    TEST_ASSERT_EQUAL(0u, s_bump_state_a.calls);
    TEST_ASSERT_EQUAL(0u, s_bump_state_b.calls);
}

void test_pipeline_no_process_format_converter_rejected(void) {
    bm_pipeline_node_t node;

    node = s_nodes[0];
    node.ops = &s_no_process_ops;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_pipeline_init(&s_pipeline, &node, 1u));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pipeline_inplace_chain);
    RUN_TEST(test_pipeline_bypass_skips_node);
    RUN_TEST(test_pipeline_format_mismatch_rejected);
    RUN_TEST(test_pipeline_separate_io_chain);
    RUN_TEST(test_pipeline_bypass_first_node_separate_io);
    RUN_TEST(test_pipeline_bypass_format_converter_rejected);
    RUN_TEST(test_pipeline_set_bypass_format_converter_rejected);
    RUN_TEST(test_pipeline_set_bypass_rejected_after_init);
    RUN_TEST(test_pipeline_bypass_rejects_small_output);
    RUN_TEST(test_pipeline_process_rejects_small_output);
    RUN_TEST(test_pipeline_no_process_format_converter_rejected);
    return UNITY_END();
}
