/* SPDX-License-Identifier: LGPL-3.0-or-later */
/**
 * @file test_can.c
 * @brief CAN/FDCAN HAL/drv 与 native_sim 后端单元测试
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 CAN/FDCAN HAL 单测
 * 2026-07-28       1.1            zeh            test_can_rx_buffered 真正验证
 *                                             RX 缓冲读出（rx_frame 接口）
 */
#include "unity.h"
#include "hal/bm_hal_can.h"
#include "bm_hal_can_native.h"
#include "bm/common/bm_types.h"

#include <stdint.h>
#include <string.h>

static uint32_t s_rx_count;
static uint32_t s_event_count;
static const struct bm_hal_can *s_rx_dev;
static const struct bm_hal_can *s_event_dev;
static bm_can_frame_t s_rx_frame;
static uint32_t s_last_event;
static void *s_rx_user;
static void *s_event_user;

void setUp(void) {
    bm_hal_can_native_reset();
    s_rx_count   = 0u;
    s_event_count = 0u;
    s_rx_dev     = NULL;
    s_event_dev  = NULL;
    (void)memset(&s_rx_frame, 0, sizeof(s_rx_frame));
    s_last_event = 0u;
    s_rx_user    = NULL;
    s_event_user = NULL;
}

void tearDown(void) {
}

static void test_can_rx_cb(const struct bm_hal_can *dev,
                           const bm_can_frame_t *frame,
                           void *user) {
    s_rx_count++;
    s_rx_dev   = dev;
    s_rx_user  = user;
    if (frame != NULL) {
        s_rx_frame = *frame;
    }
}

static void test_can_event_cb(const struct bm_hal_can *dev,
                              uint32_t event,
                              void *user) {
    s_event_count++;
    s_event_dev  = dev;
    s_last_event = event;
    s_event_user = user;
}

static void test_can_null_safety(void) {
    bm_can_frame_t frame = { 0 };
    bm_can_stats_t stats;
    bm_can_filter_t filter = { 0 };
    uint32_t filter_id;

    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_hal_can_init(NULL, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_hal_can_start(NULL));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_hal_can_stop(NULL));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_hal_can_send(NULL, &frame));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_hal_can_send(&bm_native_can1, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
        bm_hal_can_add_filter(NULL, &filter, &filter_id));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
        bm_hal_can_add_filter(&bm_native_can1, NULL, &filter_id));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
        bm_hal_can_remove_filter(NULL, 0u));
    TEST_ASSERT_EQUAL(0u, bm_hal_can_get_capabilities(NULL));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_hal_can_get_stats(NULL, &stats));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
        bm_hal_can_get_stats(&bm_native_can1, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
        bm_hal_can_set_rx_callback(NULL, test_can_rx_cb, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
        bm_hal_can_set_event_callback(NULL, test_can_event_cb, NULL));
}

static void test_can_no_backend(void) {
    const struct bm_hal_can no_backend = { NULL, NULL };
    bm_can_frame_t frame = { 0 };
    bm_can_stats_t stats;

    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_hal_can_init(&no_backend, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_hal_can_start(&no_backend));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_hal_can_stop(&no_backend));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_hal_can_send(&no_backend, &frame));
    TEST_ASSERT_EQUAL(0u, bm_hal_can_get_capabilities(&no_backend));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
        bm_hal_can_get_stats(&no_backend, &stats));
}

static void test_can_init_start_stop(void) {
    /* init 后 start 应成功；未 init 则 start 返回 NOT_INIT */
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
        bm_hal_can_start(&bm_native_can1));

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_can_init(&bm_native_can1, NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_can_start(&bm_native_can1));
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_can_stop(&bm_native_can1));
}

static void test_can_capabilities(void) {
    uint32_t caps;

    caps = bm_hal_can_get_capabilities(&bm_native_can1);
    TEST_ASSERT_NOT_EQUAL(0u, caps);
    TEST_ASSERT_BITS_HIGH(BM_CAN_CAP_FD, caps);
    TEST_ASSERT_BITS_HIGH(BM_CAN_CAP_STD_FILTER, caps);
    TEST_ASSERT_BITS_HIGH(BM_CAN_CAP_EXT_FILTER, caps);
    TEST_ASSERT_BITS_HIGH(BM_CAN_CAP_FIFO0, caps);
    TEST_ASSERT_BITS_HIGH(BM_CAN_CAP_FIFO1, caps);
    TEST_ASSERT_BITS_HIGH(BM_CAN_CAP_TX_FIFO, caps);
}

static void test_can_send_loopback(void) {
    bm_can_frame_t frame;
    bm_can_frame_t out;

    (void)memset(&frame, 0, sizeof(frame));
    frame.id   = 0x123u;
    frame.dlc  = 4u;
    frame.data[0] = 0xDEu;
    frame.data[1] = 0xADu;
    frame.data[2] = 0xBEu;
    frame.data[3] = 0xEFu;

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_can_init(&bm_native_can1, NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_can_start(&bm_native_can1));
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_can_send(&bm_native_can1, &frame));

    TEST_ASSERT_EQUAL(1u, bm_hal_can_native_tx_count(&bm_native_can1));
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_can_native_tx_frame(&bm_native_can1, &out));
    TEST_ASSERT_EQUAL(0x123u, out.id);
    TEST_ASSERT_EQUAL(4u, out.dlc);
    TEST_ASSERT_EQUAL_HEX8(0xDEu, out.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xADu, out.data[1]);
    TEST_ASSERT_EQUAL_HEX8(0xBEu, out.data[2]);
    TEST_ASSERT_EQUAL_HEX8(0xEFu, out.data[3]);
}

static void test_can_send_ext_fd(void) {
    bm_can_frame_t frame;

    (void)memset(&frame, 0, sizeof(frame));
    frame.id    = 0x1FFFFFFFu;
    frame.flags = BM_CAN_FLAG_EXT | BM_CAN_FLAG_FD | BM_CAN_FLAG_BRS;
    frame.dlc   = 64u;

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_can_init(&bm_native_can1, NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_can_send(&bm_native_can1, &frame));
    TEST_ASSERT_EQUAL(1u, bm_hal_can_native_tx_count(&bm_native_can1));
}

static void test_can_send_invalid_frame(void) {
    bm_can_frame_t frame;

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_can_init(&bm_native_can1, NULL));

    (void)memset(&frame, 0, sizeof(frame));
    frame.id  = 0x123u;
    frame.dlc = 65u; /* 超过 BM_CAN_MAX_DLC */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
        bm_hal_can_send(&bm_native_can1, &frame));

    frame.dlc = 12u; /* Classic CAN 不允许 > 8 */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
        bm_hal_can_send(&bm_native_can1, &frame));

    frame.id  = 0x800u; /* 标准帧 ID 越界 */
    frame.dlc = 4u;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
        bm_hal_can_send(&bm_native_can1, &frame));

    frame.id    = 0x20000000u; /* 扩展帧 ID 越界 */
    frame.flags = BM_CAN_FLAG_EXT;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
        bm_hal_can_send(&bm_native_can1, &frame));
}

static void test_can_filter_add_remove(void) {
    bm_can_filter_t filter;
    uint32_t id0, id1;

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_can_init(&bm_native_can1, NULL));

    (void)memset(&filter, 0, sizeof(filter));
    filter.type      = BM_CAN_FILTER_TYPE_RANGE;
    filter.id_format = BM_CAN_FILTER_STD;
    filter.fifo      = BM_CAN_FILTER_FIFO0;
    filter.id        = 0x100u;
    filter.mask      = 0x200u;
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_can_add_filter(&bm_native_can1, &filter, &id0));
    TEST_ASSERT_EQUAL(1u, bm_hal_can_native_filter_count(&bm_native_can1));

    filter.type      = BM_CAN_FILTER_TYPE_MASK;
    filter.id_format = BM_CAN_FILTER_EXT;
    filter.fifo      = BM_CAN_FILTER_FIFO1;
    filter.id        = 0x12345678u;
    filter.mask      = 0x1FFFFFFFu;
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_can_add_filter(&bm_native_can1, &filter, &id1));
    TEST_ASSERT_EQUAL(2u, bm_hal_can_native_filter_count(&bm_native_can1));

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_can_remove_filter(&bm_native_can1, id0));
    TEST_ASSERT_EQUAL(1u, bm_hal_can_native_filter_count(&bm_native_can1));
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_can_remove_filter(&bm_native_can1, id1));
    TEST_ASSERT_EQUAL(0u, bm_hal_can_native_filter_count(&bm_native_can1));
}

static void test_can_filter_invalid(void) {
    bm_can_filter_t filter;
    uint32_t id;

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_can_init(&bm_native_can1, NULL));

    (void)memset(&filter, 0, sizeof(filter));
    filter.type = 99u; /* 非法类型 */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
        bm_hal_can_add_filter(&bm_native_can1, &filter, &id));

    filter.type = BM_CAN_FILTER_TYPE_RANGE;
    filter.fifo = 99u; /* 非法 FIFO */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
        bm_hal_can_add_filter(&bm_native_can1, &filter, &id));

    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
        bm_hal_can_remove_filter(&bm_native_can1, 0u)); /* 未使用 */
}

static void test_can_filter_exhausted(void) {
    bm_can_filter_t filter;
    uint32_t id;
    size_t i;

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_can_init(&bm_native_can1, NULL));

    (void)memset(&filter, 0, sizeof(filter));
    filter.type      = BM_CAN_FILTER_TYPE_LIST;
    filter.id_format = BM_CAN_FILTER_STD;
    filter.fifo      = BM_CAN_FILTER_FIFO0;

    for (i = 0u; i < 8u; i++) {
        filter.id = (uint32_t)i;
        TEST_ASSERT_EQUAL(BM_OK,
            bm_hal_can_add_filter(&bm_native_can1, &filter, &id));
    }

    /* 第 9 个应耗尽 */
    filter.id = 9u;
    TEST_ASSERT_EQUAL(BM_ERR_NO_MEM,
        bm_hal_can_add_filter(&bm_native_can1, &filter, &id));
}

static void test_can_stats(void) {
    bm_can_frame_t frame;
    bm_can_stats_t stats;

    (void)memset(&frame, 0, sizeof(frame));
    frame.id  = 0x321u;
    frame.dlc = 2u;

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_can_init(&bm_native_can1, NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_can_send(&bm_native_can1, &frame));
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_can_send(&bm_native_can1, &frame));

    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_can_get_stats(&bm_native_can1, &stats));
    TEST_ASSERT_EQUAL(2u, stats.tx_count);
    TEST_ASSERT_EQUAL(0u, stats.rx_count);
}

static void test_can_rx_callback(void) {
    bm_can_frame_t frame;

    (void)memset(&frame, 0, sizeof(frame));
    frame.id  = 0x7F0u;
    frame.dlc = 1u;
    frame.data[0] = 0xA5u;

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_can_init(&bm_native_can1, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_can_set_rx_callback(&bm_native_can1,
                                   test_can_rx_cb, (void *)0x1234u));

    bm_hal_can_native_inject_rx(&bm_native_can1, &frame);
    TEST_ASSERT_EQUAL(1u, s_rx_count);
    TEST_ASSERT_EQUAL(&bm_native_can1, s_rx_dev);
    TEST_ASSERT_EQUAL((void *)0x1234u, s_rx_user);
    TEST_ASSERT_EQUAL(0x7F0u, s_rx_frame.id);
    TEST_ASSERT_EQUAL(1u, s_rx_frame.dlc);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, s_rx_frame.data[0]);
}

static void test_can_rx_buffered(void) {
    bm_can_frame_t frame;
    bm_can_frame_t out;

    (void)memset(&frame, 0, sizeof(frame));
    frame.id  = 0x111u;
    frame.dlc = 1u;
    frame.data[0] = 0x11u;

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_can_init(&bm_native_can1, NULL));
    /* 未注册回调，RX 应被缓冲 */
    bm_hal_can_native_inject_rx(&bm_native_can1, &frame);

    frame.id      = 0x222u;
    frame.data[0] = 0x22u;
    bm_hal_can_native_inject_rx(&bm_native_can1, &frame);
    TEST_ASSERT_EQUAL(0u, s_rx_count);

    /* 按注入顺序读出，读出后队列清空 */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_can_native_rx_frame(&bm_native_can1, &out));
    TEST_ASSERT_EQUAL(0x111u, out.id);
    TEST_ASSERT_EQUAL_HEX8(0x11u, out.data[0]);

    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_can_native_rx_frame(&bm_native_can1, &out));
    TEST_ASSERT_EQUAL(0x222u, out.id);
    TEST_ASSERT_EQUAL_HEX8(0x22u, out.data[0]);

    /* 队列已空 */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
        bm_hal_can_native_rx_frame(&bm_native_can1, &out));
}

static void test_can_event_callback(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_can_init(&bm_native_can1, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_can_set_event_callback(&bm_native_can1,
                                      test_can_event_cb, (void *)0x5678u));

    bm_hal_can_native_trigger_bus_off(&bm_native_can1);
    TEST_ASSERT_EQUAL(1u, s_event_count);
    TEST_ASSERT_EQUAL(&bm_native_can1, s_event_dev);
    TEST_ASSERT_EQUAL((void *)0x5678u, s_event_user);
    TEST_ASSERT_BITS_HIGH(BM_CAN_EVT_BUS_OFF, s_last_event);

    bm_hal_can_native_recover_bus_off(&bm_native_can1);
    TEST_ASSERT_EQUAL(2u, s_event_count);
    TEST_ASSERT_BITS_HIGH(BM_CAN_EVT_BUS_OFF_RECOVER, s_last_event);
}

static void test_can_bus_off_stats(void) {
    bm_can_stats_t stats;

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_can_init(&bm_native_can1, NULL));
    bm_hal_can_native_trigger_bus_off(&bm_native_can1);
    bm_hal_can_native_inject_event(&bm_native_can1, BM_CAN_EVT_ERROR_WARNING);

    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_can_get_stats(&bm_native_can1, &stats));
    TEST_ASSERT_EQUAL(1u, stats.bus_off_count);
    TEST_ASSERT_EQUAL(1u, stats.error_warning_count);
    TEST_ASSERT_BITS_HIGH(BM_CAN_EVT_BUS_OFF, stats.last_errors);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_can_null_safety);
    RUN_TEST(test_can_no_backend);
    RUN_TEST(test_can_init_start_stop);
    RUN_TEST(test_can_capabilities);
    RUN_TEST(test_can_send_loopback);
    RUN_TEST(test_can_send_ext_fd);
    RUN_TEST(test_can_send_invalid_frame);
    RUN_TEST(test_can_filter_add_remove);
    RUN_TEST(test_can_filter_invalid);
    RUN_TEST(test_can_filter_exhausted);
    RUN_TEST(test_can_stats);
    RUN_TEST(test_can_rx_callback);
    RUN_TEST(test_can_rx_buffered);
    RUN_TEST(test_can_event_callback);
    RUN_TEST(test_can_bus_off_stats);
    return UNITY_END();
}
