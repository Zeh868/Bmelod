/**
 * @file test_hal_dma_stream.c
 * @brief bm_hal_dma_stream / native_sim 驱动块生命周期单元测试
 *
 * 覆盖未绑定提交、重复提交、无完成回调、detach 所有权恢复与 HAL 空指针分发。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 */
#include "unity.h"
#include "hal/bm_hal_dma_stream.h"
#include "bm_hal_dma_stream_sim.h"
#include "bm/common/bm_types.h"

#include <string.h>

static uint32_t g_rx_full_count;
static bm_block_t *g_rx_full_block;

static void on_rx_full(const bm_hal_dma_stream_t *stream,
                       bm_block_t *block,
                       void *context) {
    (void)stream;
    (void)context;
    g_rx_full_count++;
    g_rx_full_block = block;
}

static void dma_reset_fixture(void) {
    bm_block_t *orphan;

    orphan = bm_hal_dma_stream_detach_rx(&BM_HAL_DMA_SIM0);
    (void)orphan;
    (void)bm_hal_dma_stream_bind_rx(&BM_HAL_DMA_SIM0, NULL);
    g_rx_full_count = 0u;
    g_rx_full_block = NULL;
}

void setUp(void) {
    dma_reset_fixture();
}

void tearDown(void) {
    dma_reset_fixture();
}

void test_submit_without_bind_returns_not_init(void) {
    bm_block_t block;
    uint8_t payload[16];

    memset(&block, 0, sizeof(block));
    block.data = payload;
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
                      bm_hal_dma_stream_submit_rx(&BM_HAL_DMA_SIM0, &block));
}

void test_submit_without_on_full_returns_not_init(void) {
    bm_hal_dma_stream_binding_t binding;
    bm_block_t block;
    uint8_t payload[16];

    memset(&binding, 0, sizeof(binding));
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_hal_dma_stream_bind_rx(&BM_HAL_DMA_SIM0, &binding));

    memset(&block, 0, sizeof(block));
    block.data = payload;
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
                      bm_hal_dma_stream_submit_rx(&BM_HAL_DMA_SIM0, &block));
}

void test_duplicate_submit_returns_busy(void) {
    bm_hal_dma_stream_binding_t binding = {
        .on_half = NULL,
        .on_full = on_rx_full,
        .context = NULL
    };
    bm_block_t block;
    uint8_t payload[16];

    TEST_ASSERT_EQUAL(BM_OK,
                      bm_hal_dma_stream_bind_rx(&BM_HAL_DMA_SIM0, &binding));
    memset(&block, 0, sizeof(block));
    block.data = payload;
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_hal_dma_stream_submit_rx(&BM_HAL_DMA_SIM0, &block));
    TEST_ASSERT_EQUAL(BM_ERR_BUSY,
                      bm_hal_dma_stream_submit_rx(&BM_HAL_DMA_SIM0, &block));

    bm_hal_dma_stream_sim_fire_rx_complete(&BM_HAL_DMA_SIM0);
    TEST_ASSERT_EQUAL(1u, g_rx_full_count);
    TEST_ASSERT_EQUAL_PTR(&block, g_rx_full_block);
}

void test_detach_rx_returns_active_block_without_callback(void) {
    bm_hal_dma_stream_binding_t binding = {
        .on_full = on_rx_full,
        .context = NULL
    };
    bm_block_t block;
    uint8_t payload[16];
    bm_block_t *detached;

    TEST_ASSERT_EQUAL(BM_OK,
                      bm_hal_dma_stream_bind_rx(&BM_HAL_DMA_SIM0, &binding));
    memset(&block, 0, sizeof(block));
    block.data = payload;
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_hal_dma_stream_submit_rx(&BM_HAL_DMA_SIM0, &block));

    detached = bm_hal_dma_stream_detach_rx(&BM_HAL_DMA_SIM0);
    TEST_ASSERT_EQUAL_PTR(&block, detached);
    TEST_ASSERT_EQUAL(0u, g_rx_full_count);

    TEST_ASSERT_EQUAL(BM_OK,
                      bm_hal_dma_stream_submit_rx(&BM_HAL_DMA_SIM0, &block));
    bm_hal_dma_stream_sim_fire_rx_complete(&BM_HAL_DMA_SIM0);
    TEST_ASSERT_EQUAL(1u, g_rx_full_count);
}

void test_detach_rx_returns_null_when_idle(void) {
    TEST_ASSERT_NULL(bm_hal_dma_stream_detach_rx(&BM_HAL_DMA_SIM0));
}

void test_hal_null_stream_returns_not_init(void) {
    bm_hal_dma_stream_binding_t binding = { .on_full = on_rx_full };
    bm_block_t block;
    uint8_t payload[16];

    memset(&block, 0, sizeof(block));
    block.data = payload;
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
                      bm_hal_dma_stream_bind_rx(NULL, &binding));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
                      bm_hal_dma_stream_submit_rx(NULL, &block));
    TEST_ASSERT_NULL(bm_hal_dma_stream_detach_rx(NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_submit_without_bind_returns_not_init);
    RUN_TEST(test_submit_without_on_full_returns_not_init);
    RUN_TEST(test_duplicate_submit_returns_busy);
    RUN_TEST(test_detach_rx_returns_active_block_without_callback);
    RUN_TEST(test_detach_rx_returns_null_when_idle);
    RUN_TEST(test_hal_null_stream_returns_not_init);
    return UNITY_END();
}
