/**
 * @file test_recorder.c
 * @brief 录波环 bm_recorder 顺序/回绕覆盖/撕裂读校验/dropped 计数单元测试
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-21
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-06-21       1.0            zeh            正式发布
 */

#include "unity.h"
#include "bm/hybrid/bm_recorder.h"

#include <string.h>

/** 测试帧：带 seq 与填充，便于校验数据完整性。 */
typedef struct {
    uint32_t seq;
    uint32_t a;
    uint32_t b;
    uint32_t c;
} test_frame_t;

#define TEST_DEPTH 8u

BM_RECORDER_DEFINE(g_rec, test_frame_t, TEST_DEPTH);

void setUp(void) {
    bm_recorder_reset(&g_rec);
}

void tearDown(void) {}

/** 构造一帧 */
static test_frame_t make_frame(uint32_t seq) {
    test_frame_t f;
    f.seq = seq;
    f.a = seq + 100u;
    f.b = seq + 200u;
    f.c = seq + 300u;
    return f;
}

/** 用例①：基本 push→pop 顺序与数据完整 */
void test_recorder_basic_order(void) {
    test_frame_t in;
    test_frame_t out;
    uint32_t i;

    for (i = 0u; i < 4u; ++i) {
        in = make_frame(i);
        bm_recorder_push(&g_rec, &in);
    }
    TEST_ASSERT_EQUAL_UINT32(4u, bm_recorder_count(&g_rec));

    for (i = 0u; i < 4u; ++i) {
        TEST_ASSERT_EQUAL_INT(1, bm_recorder_pop(&g_rec, &out));
        TEST_ASSERT_EQUAL_UINT32(i, out.seq);
        TEST_ASSERT_EQUAL_UINT32(i + 100u, out.a);
        TEST_ASSERT_EQUAL_UINT32(i + 200u, out.b);
        TEST_ASSERT_EQUAL_UINT32(i + 300u, out.c);
    }
    TEST_ASSERT_EQUAL_INT(0, bm_recorder_pop(&g_rec, &out));
    TEST_ASSERT_EQUAL_UINT32(0u, bm_recorder_dropped(&g_rec));
}

/** 用例②：写满未读再写（回绕）覆盖最旧，pop 拿到最新 capacity 帧，dropped 正确 */
void test_recorder_wrap_overwrite(void) {
    test_frame_t in;
    test_frame_t out;
    uint32_t i;
    uint32_t total = TEST_DEPTH + 5u; /* 写 13 帧，环深 8 */

    for (i = 0u; i < total; ++i) {
        in = make_frame(i);
        bm_recorder_push(&g_rec, &in);
    }
    /* 可读封顶 capacity */
    TEST_ASSERT_EQUAL_UINT32(TEST_DEPTH, bm_recorder_count(&g_rec));

    /* 首次 pop 触发跳到最旧有效帧：seq 应为 total-capacity = 5 */
    TEST_ASSERT_EQUAL_INT(1, bm_recorder_pop(&g_rec, &out));
    TEST_ASSERT_EQUAL_UINT32(total - TEST_DEPTH, out.seq);
    /* 被覆盖丢弃 = total-capacity = 5 */
    TEST_ASSERT_EQUAL_UINT32(total - TEST_DEPTH, bm_recorder_dropped(&g_rec));

    /* 其余 capacity-1 帧顺序应为 6..12 */
    for (i = total - TEST_DEPTH + 1u; i < total; ++i) {
        TEST_ASSERT_EQUAL_INT(1, bm_recorder_pop(&g_rec, &out));
        TEST_ASSERT_EQUAL_UINT32(i, out.seq);
        TEST_ASSERT_EQUAL_UINT32(i + 300u, out.c);
    }
    TEST_ASSERT_EQUAL_INT(0, bm_recorder_pop(&g_rec, &out));
}

/** 用例③：bm_recorder_init 拒绝非 2 的幂 / <2 / 非法参数 */
void test_recorder_init_validation(void) {
    bm_recorder_t r;
    uint8_t buf[64];

    /* 非 2 的幂 */
    TEST_ASSERT_EQUAL_INT(BM_ERR_INVALID,
        bm_recorder_init(&r, buf, 4u, 3u, 0u));
    TEST_ASSERT_EQUAL_INT(BM_ERR_INVALID,
        bm_recorder_init(&r, buf, 4u, 6u, 0u));
    /* <2 */
    TEST_ASSERT_EQUAL_INT(BM_ERR_INVALID,
        bm_recorder_init(&r, buf, 4u, 1u, 0u));
    TEST_ASSERT_EQUAL_INT(BM_ERR_INVALID,
        bm_recorder_init(&r, buf, 4u, 0u, 0u));
    /* buf/elem_size 非法 */
    TEST_ASSERT_EQUAL_INT(BM_ERR_INVALID,
        bm_recorder_init(&r, NULL, 4u, 8u, 0u));
    TEST_ASSERT_EQUAL_INT(BM_ERR_INVALID,
        bm_recorder_init(&r, buf, 0u, 8u, 0u));
    TEST_ASSERT_EQUAL_INT(BM_ERR_INVALID,
        bm_recorder_init(NULL, buf, 4u, 8u, 0u));

    /* 合法：2 的幂 >=2 */
    TEST_ASSERT_EQUAL_INT(BM_OK,
        bm_recorder_init(&r, buf, 4u, 8u, 1u));
    TEST_ASSERT_EQUAL_UINT32(8u, r.capacity);
    TEST_ASSERT_EQUAL_UINT32(7u, r.mask);
    TEST_ASSERT_EQUAL_UINT32(0u, bm_recorder_count(&r));
}

/** drain sink：累加 seq 之和并计数 */
static void sum_sink(const void *f, void *ctx) {
    const test_frame_t *frame = (const test_frame_t *)f;
    uint32_t *acc = (uint32_t *)ctx;
    acc[0] += frame->seq;
    acc[1] += 1u;
}

/** 用例④：count / drain(budget) 行为 */
void test_recorder_count_and_drain_budget(void) {
    test_frame_t in;
    uint32_t acc[2];
    uint32_t i;

    for (i = 0u; i < 6u; ++i) {
        in = make_frame(i);
        bm_recorder_push(&g_rec, &in);
    }
    TEST_ASSERT_EQUAL_UINT32(6u, bm_recorder_count(&g_rec));

    /* budget=3：只取 3 帧（seq 0,1,2，和=3） */
    acc[0] = 0u;
    acc[1] = 0u;
    TEST_ASSERT_EQUAL_UINT32(3u, bm_recorder_drain(&g_rec, sum_sink, acc, 3u));
    TEST_ASSERT_EQUAL_UINT32(3u, acc[1]);
    TEST_ASSERT_EQUAL_UINT32(0u + 1u + 2u, acc[0]);
    TEST_ASSERT_EQUAL_UINT32(3u, bm_recorder_count(&g_rec));

    /* budget=0：取空剩余 3 帧（seq 3,4,5，和=12） */
    acc[0] = 0u;
    acc[1] = 0u;
    TEST_ASSERT_EQUAL_UINT32(3u, bm_recorder_drain(&g_rec, sum_sink, acc, 0u));
    TEST_ASSERT_EQUAL_UINT32(3u, acc[1]);
    TEST_ASSERT_EQUAL_UINT32(3u + 4u + 5u, acc[0]);
    TEST_ASSERT_EQUAL_UINT32(0u, bm_recorder_count(&g_rec));

    /* 空环 drain 返 0；sink=NULL 也安全 */
    TEST_ASSERT_EQUAL_UINT32(0u, bm_recorder_drain(&g_rec, NULL, NULL, 0u));
}

/** 用例⑤：交替 push/pop 长跑（>=10×capacity）数据一致、无越界 */
void test_recorder_long_run_consistency(void) {
    test_frame_t in;
    test_frame_t out;
    uint32_t n = TEST_DEPTH * 20u;
    uint32_t i;

    for (i = 0u; i < n; ++i) {
        in = make_frame(i);
        bm_recorder_push(&g_rec, &in);
        /* 每 push 一帧立即 pop 一帧：环始终不溢出，FIFO 一致 */
        TEST_ASSERT_EQUAL_INT(1, bm_recorder_pop(&g_rec, &out));
        TEST_ASSERT_EQUAL_UINT32(i, out.seq);
        TEST_ASSERT_EQUAL_UINT32(i + 100u, out.a);
        TEST_ASSERT_EQUAL_UINT32(i + 300u, out.c);
    }
    TEST_ASSERT_EQUAL_UINT32(0u, bm_recorder_count(&g_rec));
    TEST_ASSERT_EQUAL_UINT32(0u, bm_recorder_dropped(&g_rec));
}

/** 用例⑥：持续 overrun 下 dropped 单调正确 */
void test_recorder_dropped_monotonic_overrun(void) {
    test_frame_t in;
    test_frame_t out;
    uint32_t round;
    uint32_t i;
    uint32_t seq = 0u;
    uint32_t prev_dropped = 0u;

    /* 多轮：每轮塞满 2×capacity 后只读 1 帧，制造持续覆盖 */
    for (round = 0u; round < 5u; ++round) {
        for (i = 0u; i < TEST_DEPTH * 2u; ++i) {
            in = make_frame(seq++);
            bm_recorder_push(&g_rec, &in);
        }
        TEST_ASSERT_EQUAL_UINT32(TEST_DEPTH, bm_recorder_count(&g_rec));
        TEST_ASSERT_EQUAL_INT(1, bm_recorder_pop(&g_rec, &out));
        /* dropped 必须单调不减且每轮确有增长（持续 overrun） */
        TEST_ASSERT_GREATER_THAN_UINT32(prev_dropped,
            bm_recorder_dropped(&g_rec));
        prev_dropped = bm_recorder_dropped(&g_rec);
        /* 读出的应是当前最旧有效帧，seq 落在合法范围内 */
        TEST_ASSERT_TRUE(out.seq < seq);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_recorder_basic_order);
    RUN_TEST(test_recorder_wrap_overwrite);
    RUN_TEST(test_recorder_init_validation);
    RUN_TEST(test_recorder_count_and_drain_budget);
    RUN_TEST(test_recorder_long_run_consistency);
    RUN_TEST(test_recorder_dropped_monotonic_overrun);
    return UNITY_END();
}
