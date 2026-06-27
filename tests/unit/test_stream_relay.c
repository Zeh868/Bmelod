/**
 * @file test_stream_relay.c
 * @brief BM_STREAM_RELAY 跨核 relay 单元测试
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-19
 */
#include "unity.h"
#include "bm/hybrid/bm_stream_relay.h"
#include "bm_hal_cpu_mp_native.h"

#include <string.h>

#define RELAY_SLOT_BYTES  16u
#define RELAY_DEPTH       4u

typedef struct {
    uint32_t count;
    uint32_t sequence[RELAY_DEPTH];
    uint32_t length[RELAY_DEPTH];
    uint8_t payload[RELAY_DEPTH][RELAY_SLOT_BYTES];
} relay_capture_t;

BM_STREAM_RELAY_SLOTS(s_relay, RELAY_DEPTH, RELAY_SLOT_BYTES);
static BM_CACHE_ALIGNAS(BM_CONFIG_CACHE_LINE) bm_stream_relay_t s_relay = {
    .source_cpu = 0u,
    .target_cpu = 1u,
    .slot_bytes = RELAY_SLOT_BYTES,
    .depth = RELAY_DEPTH,
    .slots = _bm_stream_relay_slots_s_relay,
    .cache_policy = BM_STREAM_RELAY_CACHE_NON_CACHEABLE,
};

static relay_capture_t g_capture;

/**
 * @brief 复位 relay 测试夹具。
 */
static void relay_reset_fixture(void) {
    memset(&s_relay, 0, sizeof(s_relay));
    s_relay.source_cpu = 0u;
    s_relay.target_cpu = 1u;
    s_relay.slot_bytes = RELAY_SLOT_BYTES;
    s_relay.depth = RELAY_DEPTH;
    s_relay.slots = _bm_stream_relay_slots_s_relay;
    s_relay.cache_policy = BM_STREAM_RELAY_CACHE_NON_CACHEABLE;
    memset(&g_capture, 0, sizeof(g_capture));
}

/**
 * @brief relay 消费回调，记录收到的 payload。
 */
static void relay_consume(bm_stream_relay_t *relay,
                          const void *payload,
                          uint32_t len,
                          uint32_t sequence,
                          void *context) {
    relay_capture_t *capture = (relay_capture_t *)context;
    uint32_t index;

    TEST_ASSERT_NOT_NULL(relay);
    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_NOT_NULL(capture);
    TEST_ASSERT_TRUE(capture->count < RELAY_DEPTH);

    index = capture->count;
    capture->sequence[index] = sequence;
    capture->length[index] = len;
    memcpy(capture->payload[index], payload, len);
    capture->count++;
}

void setUp(void) {
    bm_hal_cpu_init();
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(0u));
    bm_stream_relay_registry_reset();
    relay_reset_fixture();
}

void tearDown(void) {
    bm_stream_relay_registry_reset();
    (void)bm_hal_cpu_native_set_id(0u);
}

void test_stream_relay_delivers_payloads_between_cpus(void) {
    static const uint8_t first_payload[] = {0x11u, 0x22u, 0x33u, 0x44u};
    static const uint8_t second_payload[] = {0xAAu, 0xBBu, 0xCCu, 0xDDu};
    const bm_stream_relay_stats_t *stats;

    TEST_ASSERT_EQUAL(BM_OK, bm_stream_relay_init(&s_relay));
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(1u));
    TEST_ASSERT_EQUAL(
        BM_OK,
        bm_stream_relay_register_on_this_cpu(&s_relay, relay_consume, &g_capture));
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(0u));
    TEST_ASSERT_EQUAL(
        BM_OK,
        bm_stream_relay_publish(&s_relay, first_payload, sizeof(first_payload)));
    TEST_ASSERT_EQUAL(
        BM_OK,
        bm_stream_relay_publish(&s_relay, second_payload, sizeof(second_payload)));

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(1u));
    TEST_ASSERT_EQUAL_INT(2, bm_stream_relay_drain_on_this_cpu(4u));

    stats = bm_stream_relay_stats(&s_relay);
    TEST_ASSERT_NOT_NULL(stats);
    TEST_ASSERT_EQUAL_UINT32(2u, g_capture.count);
    TEST_ASSERT_EQUAL_UINT32(1u, g_capture.sequence[0]);
    TEST_ASSERT_EQUAL_UINT32(2u, g_capture.sequence[1]);
    TEST_ASSERT_EQUAL_UINT32(sizeof(first_payload), g_capture.length[0]);
    TEST_ASSERT_EQUAL_UINT32(sizeof(second_payload), g_capture.length[1]);
    TEST_ASSERT_EQUAL_MEMORY(first_payload, g_capture.payload[0],
                             sizeof(first_payload));
    TEST_ASSERT_EQUAL_MEMORY(second_payload, g_capture.payload[1],
                             sizeof(second_payload));
    TEST_ASSERT_EQUAL_UINT32(2u, stats->delivered);
    TEST_ASSERT_EQUAL_UINT32(0u, stats->drop);
    TEST_ASSERT_EQUAL_UINT32(0u, stats->corrupt_dropped);
}

void test_stream_relay_overflow_counts_drop(void) {
    static const uint8_t payload0[] = {0x01u, 0x10u};
    static const uint8_t payload1[] = {0x02u, 0x20u};
    static const uint8_t payload2[] = {0x03u, 0x30u};
    static const uint8_t payload3[] = {0x04u, 0x40u};
    const bm_stream_relay_stats_t *stats;

    TEST_ASSERT_EQUAL(BM_OK, bm_stream_relay_init(&s_relay));
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(1u));
    TEST_ASSERT_EQUAL(
        BM_OK,
        bm_stream_relay_register_on_this_cpu(&s_relay, relay_consume, &g_capture));
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(0u));
    TEST_ASSERT_EQUAL(BM_OK, bm_stream_relay_publish(&s_relay, payload0, sizeof(payload0)));
    TEST_ASSERT_EQUAL(BM_OK, bm_stream_relay_publish(&s_relay, payload1, sizeof(payload1)));
    TEST_ASSERT_EQUAL(BM_OK, bm_stream_relay_publish(&s_relay, payload2, sizeof(payload2)));
    TEST_ASSERT_EQUAL(BM_ERR_OVERFLOW, bm_stream_relay_publish(&s_relay, payload3, sizeof(payload3)));

    stats = bm_stream_relay_stats(&s_relay);
    TEST_ASSERT_NOT_NULL(stats);
    TEST_ASSERT_EQUAL_UINT32(1u, stats->drop);

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(1u));
    TEST_ASSERT_EQUAL_INT(3, bm_stream_relay_drain_on_this_cpu(4u));

    TEST_ASSERT_EQUAL_UINT32(3u, g_capture.count);
    TEST_ASSERT_EQUAL_MEMORY(payload0, g_capture.payload[0], sizeof(payload0));
    TEST_ASSERT_EQUAL_MEMORY(payload1, g_capture.payload[1], sizeof(payload1));
    TEST_ASSERT_EQUAL_MEMORY(payload2, g_capture.payload[2], sizeof(payload2));
    TEST_ASSERT_EQUAL_UINT32(1u, g_capture.sequence[0]);
    TEST_ASSERT_EQUAL_UINT32(2u, g_capture.sequence[1]);
    TEST_ASSERT_EQUAL_UINT32(3u, g_capture.sequence[2]);
    TEST_ASSERT_EQUAL_UINT32(3u, stats->delivered);
    TEST_ASSERT_EQUAL_UINT32(1u, stats->drop);
    TEST_ASSERT_EQUAL_UINT32(0u, stats->corrupt_dropped);
}

void test_stream_relay_freeze_blocks_late_registration(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_stream_relay_init(&s_relay));
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(1u));
    TEST_ASSERT_EQUAL(
        BM_OK,
        bm_stream_relay_register_on_this_cpu(&s_relay, relay_consume, &g_capture));
    bm_stream_relay_freeze_registry();
    TEST_ASSERT_EQUAL(
        BM_ERR_BUSY,
        bm_stream_relay_register_on_this_cpu(&s_relay, relay_consume, &g_capture));
}

void test_stream_relay_init_rejects_wrong_source_cpu(void) {
    relay_reset_fixture();
    s_relay.source_cpu = 1u;
    s_relay.target_cpu = 0u;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_stream_relay_init(&s_relay));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stream_relay_delivers_payloads_between_cpus);
    RUN_TEST(test_stream_relay_overflow_counts_drop);
    RUN_TEST(test_stream_relay_freeze_blocks_late_registration);
    RUN_TEST(test_stream_relay_init_rejects_wrong_source_cpu);
    return UNITY_END();
}
