/**
 * @file test_channel.c
 * @brief 消息通道 ring buffer 收发、溢出与回绕单元测试
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 */

#include "unity.h"
#include "bm_channel.h"
#include "bm_log.h"

typedef struct {
    uint32_t a;
    uint16_t b;
} test_msg_t;

BM_CHANNEL_DEFINE(my_chan, test_msg_t, 4);

void setUp(void) {
    BM_LOGI("test_chan", "setUp: reset channel");
    bm_channel_reset(&my_chan);
}
void tearDown(void) {}

void test_channel_send_recv(void) {
    test_msg_t msg = { .a = 123, .b = 456 };
    TEST_ASSERT_EQUAL(BM_OK, bm_channel_send(&my_chan, &msg));
    TEST_ASSERT_EQUAL(1, bm_channel_count(&my_chan));
    TEST_ASSERT_FALSE(bm_channel_is_empty(&my_chan));

    test_msg_t out = {0};
    TEST_ASSERT_EQUAL(BM_OK, bm_channel_recv(&my_chan, &out));
    TEST_ASSERT_EQUAL(123, out.a);
    TEST_ASSERT_EQUAL(456, out.b);
    TEST_ASSERT_TRUE(bm_channel_is_empty(&my_chan));
}

void test_channel_overflow(void) {
    /* 容量 4，保留 1 槽区分满/空 */
    test_msg_t msg = { .a = 1, .b = 2 };
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(BM_OK, bm_channel_send(&my_chan, &msg));
    }
    TEST_ASSERT_TRUE(bm_channel_is_full(&my_chan));
    BM_LOGE("test_chan", "expect OVERFLOW on full channel send");
    TEST_ASSERT_EQUAL(BM_ERR_OVERFLOW, bm_channel_send(&my_chan, &msg));
}

void test_channel_underflow(void) {
    test_msg_t out = {0};
    TEST_ASSERT_TRUE(bm_channel_is_empty(&my_chan));
    BM_LOGE("test_chan", "expect WOULD_BLOCK on empty channel recv");
    TEST_ASSERT_EQUAL(BM_ERR_WOULD_BLOCK, bm_channel_recv(&my_chan, &out));
}

void test_channel_invalid_args(void) {
    test_msg_t msg = { .a = 1, .b = 2 };
    test_msg_t out = {0};

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_channel_send(NULL, &msg));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_channel_send(&my_chan, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_channel_recv(NULL, &out));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_channel_recv(&my_chan, NULL));

    my_chan.write_idx = 99u;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_channel_send(&my_chan, &msg));
    bm_channel_reset(&my_chan);

    my_chan.read_idx = 99u;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_channel_recv(&my_chan, &out));
    bm_channel_reset(&my_chan);
}

void test_channel_reset_and_capacity(void) {
    test_msg_t msg = { .a = 7, .b = 8 };
    TEST_ASSERT_EQUAL(BM_OK, bm_channel_send(&my_chan, &msg));
    bm_channel_reset(&my_chan);
    TEST_ASSERT_TRUE(bm_channel_is_empty(&my_chan));
    TEST_ASSERT_EQUAL(4u, bm_channel_capacity(&my_chan));
}

void test_channel_query_consistency(void) {
    test_msg_t msg = { .a = 1, .b = 2 };
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_FALSE(bm_channel_is_full(&my_chan));
        TEST_ASSERT_EQUAL(BM_OK, bm_channel_send(&my_chan, &msg));
    }
    TEST_ASSERT_TRUE(bm_channel_is_full(&my_chan));
    TEST_ASSERT_EQUAL(3u, bm_channel_count(&my_chan));
}

void test_channel_wraparound(void) {
    test_msg_t msg = { .a = 42, .b = 0 };
    for (int i = 0; i < 3; i++) {
        msg.a = (uint32_t)i;
        TEST_ASSERT_EQUAL(BM_OK, bm_channel_send(&my_chan, &msg));
    }

    /* 收 2 发 2，迫使环形缓冲回绕 */
    test_msg_t out;
    bm_channel_recv(&my_chan, &out);
    bm_channel_recv(&my_chan, &out);

    msg.a = 100;
    TEST_ASSERT_EQUAL(BM_OK, bm_channel_send(&my_chan, &msg));
    msg.a = 101;
    TEST_ASSERT_EQUAL(BM_OK, bm_channel_send(&my_chan, &msg));

    /* 排空并校验顺序 */
    bm_channel_recv(&my_chan, &out);
    TEST_ASSERT_EQUAL(2, out.a);
    bm_channel_recv(&my_chan, &out);
    TEST_ASSERT_EQUAL(100, out.a);
    bm_channel_recv(&my_chan, &out);
    TEST_ASSERT_EQUAL(101, out.a);
    TEST_ASSERT_TRUE(bm_channel_is_empty(&my_chan));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_channel_send_recv);
    RUN_TEST(test_channel_overflow);
    RUN_TEST(test_channel_underflow);
    RUN_TEST(test_channel_wraparound);
    RUN_TEST(test_channel_invalid_args);
    RUN_TEST(test_channel_reset_and_capacity);
    RUN_TEST(test_channel_query_consistency);
    return UNITY_END();
}
