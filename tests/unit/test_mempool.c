/**
 * @file test_mempool.c
 * @brief 固定大小内存池分配、释放与耗尽单元测试
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 */

#include "unity.h"
#include "bm_core.h"
#include "bm_log.h"

#include <string.h>

typedef struct { uint32_t a; uint8_t b; } test_obj_t;

void setUp(void) {
    BM_LOGI("test_pool", "setUp");
}
void tearDown(void) {}

void test_mempool_alloc_free(void) {
    BM_MEMPOOL_DEFINE(pool, test_obj_t, 4);
    memset(pool.bitmap, 0, pool.bitmap_words * sizeof(uint32_t));

    test_obj_t *a = (test_obj_t *)bm_mempool_alloc(&pool);
    TEST_ASSERT_NOT_NULL(a);
    a->a = 123;

    bm_mempool_free(&pool, a);
    test_obj_t *b = (test_obj_t *)bm_mempool_alloc(&pool);
    TEST_ASSERT_EQUAL_PTR(a, b);
}

void test_mempool_double_free_ignored(void) {
    BM_MEMPOOL_DEFINE(pool, test_obj_t, 2);
    memset(pool.bitmap, 0, pool.bitmap_words * sizeof(uint32_t));

    test_obj_t *a = (test_obj_t *)bm_mempool_alloc(&pool);
    TEST_ASSERT_NOT_NULL(a);
    bm_mempool_free(&pool, a);
    bm_mempool_free(&pool, a);
    TEST_ASSERT_NOT_NULL(bm_mempool_alloc(&pool));
}

void test_mempool_alloc_zeroed(void) {
    BM_MEMPOOL_DEFINE(pool, test_obj_t, 2);
    memset(pool.bitmap, 0, pool.bitmap_words * sizeof(uint32_t));
    memset(pool.pool, 0xFF, pool.obj_size * pool.count);

    test_obj_t *a = (test_obj_t *)bm_mempool_alloc(&pool);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL(0u, a->a);
    TEST_ASSERT_EQUAL(0u, a->b);
    bm_mempool_free(&pool, a);
}

void test_mempool_free_out_of_range(void) {
    BM_MEMPOOL_DEFINE(pool, test_obj_t, 2);
    test_obj_t rogue = { .a = 1u, .b = 2u };

    memset(pool.bitmap, 0, pool.bitmap_words * sizeof(uint32_t));
    bm_mempool_free(&pool, &rogue);
    TEST_ASSERT_NOT_NULL(bm_mempool_alloc(&pool));
}

void test_mempool_free_misaligned(void) {
    BM_MEMPOOL_DEFINE(pool, test_obj_t, 2);

    memset(pool.bitmap, 0, pool.bitmap_words * sizeof(uint32_t));
    test_obj_t *a = (test_obj_t *)bm_mempool_alloc(&pool);
    TEST_ASSERT_NOT_NULL(a);
    bm_mempool_free(&pool, (void *)((uint8_t *)a + 1u));
    test_obj_t *b = (test_obj_t *)bm_mempool_alloc(&pool);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_EQUAL(a, b);
    TEST_ASSERT_NULL(bm_mempool_alloc(&pool));
    bm_mempool_reset(&pool);
    TEST_ASSERT_NOT_NULL(bm_mempool_alloc(&pool));
}

void test_mempool_reset(void) {
    BM_MEMPOOL_DEFINE(pool, test_obj_t, 2);

    memset(pool.bitmap, 0, pool.bitmap_words * sizeof(uint32_t));
    TEST_ASSERT_NOT_NULL(bm_mempool_alloc(&pool));
    bm_mempool_reset(&pool);
    TEST_ASSERT_NOT_NULL(bm_mempool_alloc(&pool));
    TEST_ASSERT_NOT_NULL(bm_mempool_alloc(&pool));
}

void test_mempool_exhausted(void) {
    BM_MEMPOOL_DEFINE(pool, test_obj_t, 2);
    memset(pool.bitmap, 0, pool.bitmap_words * sizeof(uint32_t));

    TEST_ASSERT_NOT_NULL(bm_mempool_alloc(&pool));
    TEST_ASSERT_NOT_NULL(bm_mempool_alloc(&pool));
    BM_LOGE("test_pool", "expect NULL when pool exhausted");
    TEST_ASSERT_NULL(bm_mempool_alloc(&pool));
}

void test_mempool_rejects_overflowing_descriptor(void) {
    uint32_t bitmap = 0u;
    uint8_t storage[8] = {0};
    bm_mempool_t pool = {
        .bitmap = &bitmap,
        .pool = storage,
        .obj_size = SIZE_MAX,
        .count = 2u,
        .bitmap_words = 1u,
    };

    TEST_ASSERT_NULL(bm_mempool_alloc(&pool));
    bm_mempool_free(&pool, storage);
}

void test_mempool_ignores_excess_bitmap_words(void) {
    uint32_t bitmap[2] = {0u, 0xA5A5A5A5u};
    uint8_t storage[2] = {0};
    bm_mempool_t pool = {
        .bitmap = bitmap,
        .pool = storage,
        .obj_size = 1u,
        .count = 2u,
        .bitmap_words = 2u,
    };

    TEST_ASSERT_NOT_NULL(bm_mempool_alloc(&pool));
    TEST_ASSERT_NOT_NULL(bm_mempool_alloc(&pool));
    TEST_ASSERT_NULL(bm_mempool_alloc(&pool));
    bm_mempool_reset(&pool);
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, bitmap[1]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mempool_alloc_free);
    RUN_TEST(test_mempool_alloc_zeroed);
    RUN_TEST(test_mempool_free_out_of_range);
    RUN_TEST(test_mempool_free_misaligned);
    RUN_TEST(test_mempool_reset);
    RUN_TEST(test_mempool_exhausted);
    RUN_TEST(test_mempool_double_free_ignored);
    RUN_TEST(test_mempool_rejects_overflowing_descriptor);
    RUN_TEST(test_mempool_ignores_excess_bitmap_words);
    return UNITY_END();
}
