/* SPDX-License-Identifier: LGPL-3.0-or-later */
/**
 * @file test_mask_hrt_guard.c
 * @brief 掩码模式（BM_CONFIG_ENABLE_PRIORITY_MASK=1）下 SRT 队列 API 对
 *        HRT 级 ISR 上下文 fail-closed 的运行期拦截单元测试
 *
 * 测试目标直接编译 Source/core/bm_event.c 与 bm_mempool.c（掩码宏开），
 * 临界区两层符号由 fakes/bm_mask_guard_fake.c 提供。断言：
 * - HRT ISR 上下文（bm_hrt_isr_enter 标记）中：
 *   bm_event_publish_copy_from_isr → BM_ERR_BUSY（且不触阈值掩码临界区）、
 *   bm_mempool_alloc → NULL、bm_mempool_try_free → BM_ERR_BUSY、
 *   bm_mempool_reset 被拒绝（池内容不变）；
 * - 低于 HRT 阈值的 from_isr 与普通主循环路径不受影响（BM_OK）；
 * - 正常入队确实以 BM_CONFIG_HRT_PRIORITY_THRESHOLD 进入阈值掩码临界区。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-30
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-07-30       1.0            zeh            正式发布
 */

#include "unity.h"
#include "bm/core/bm_event.h"
#include "bm/core/bm_mempool_impl.h"
#include "bm/common/bm_critical_wrap.h"
#include "bm/common/bm_types.h"

#include <stdint.h>

/* fakes/bm_mask_guard_fake.c 访问器 */
extern uint8_t bm_mask_guard_fake_last_threshold(void);
extern unsigned bm_mask_guard_fake_enter_below_count(void);
extern void bm_mask_guard_fake_reset(void);

#define GUARD_EVT_TYPE ((bm_event_type_t)0x0Du)
#define GUARD_EVT_PRIO ((bm_event_priority_t)0u)

typedef struct {
    uint32_t word;
} guard_obj_t;

BM_MEMPOOL_DEFINE(g_pool, guard_obj_t, 4u);

void setUp(void) {
    bm_mask_guard_fake_reset();
    bm_event_reset();
    /* 计数器跨用例必须保持平衡；此处防御性确认无残留 HRT 上下文 */
    TEST_ASSERT_EQUAL_INT(0, bm_in_hrt_isr());
}

void tearDown(void) {}

/* 低于 HRT 阈值的普通 ISR 发布：允许，且以阈值掩码临界区入队 */
void test_publish_from_below_threshold_isr_ok(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_event_register_type(GUARD_EVT_TYPE, "guard"));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_event_publish_copy_from_isr(GUARD_EVT_TYPE, GUARD_EVT_PRIO,
                                       NULL, 0u));
    /* 入队走了阈值掩码临界区，阈值为 HRT 优先级阈值 */
    TEST_ASSERT_EQUAL_UINT8((uint8_t)BM_CONFIG_HRT_PRIORITY_THRESHOLD,
                            bm_mask_guard_fake_last_threshold());
}

/* HRT 级 ISR 发布：fail-closed BM_ERR_BUSY，且不得触碰队列临界区 */
void test_publish_from_hrt_isr_rejected(void) {
    unsigned before;

    TEST_ASSERT_EQUAL(BM_OK, bm_event_register_type(GUARD_EVT_TYPE, "guard"));

    bm_hrt_isr_enter();
    before = bm_mask_guard_fake_enter_below_count();
    TEST_ASSERT_EQUAL(BM_ERR_BUSY,
        bm_event_publish_copy_from_isr(GUARD_EVT_TYPE, GUARD_EVT_PRIO,
                                       NULL, 0u));
    /* 拦截发生在路由转发与入队之前：阈值掩码临界区未被进入 */
    TEST_ASSERT_EQUAL_UINT(before, bm_mask_guard_fake_enter_below_count());
    bm_hrt_isr_exit();

    /* 退出 HRT 上下文后同一调用恢复可用 */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_event_publish_copy_from_isr(GUARD_EVT_TYPE, GUARD_EVT_PRIO,
                                       NULL, 0u));
}

/* 主循环路径在掩码模式下不受影响 */
void test_publish_normal_path_ok(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_event_register_type(GUARD_EVT_TYPE, "guard"));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_event_publish_copy(GUARD_EVT_TYPE, GUARD_EVT_PRIO, NULL, 0u));
}

/* mempool：HRT 级 ISR 中 alloc/try_free 均 fail-closed */
void test_mempool_hrt_isr_rejected(void) {
    void *obj = bm_mempool_alloc(&g_pool);

    TEST_ASSERT_NOT_NULL(obj);

    bm_hrt_isr_enter();
    TEST_ASSERT_NULL(bm_mempool_alloc(&g_pool));
    TEST_ASSERT_EQUAL(BM_ERR_BUSY, bm_mempool_try_free(&g_pool, obj));
    bm_hrt_isr_exit();

    /* 退出 HRT 上下文后归还成功，证明拦截期间位图未被破坏 */
    TEST_ASSERT_EQUAL(BM_OK, bm_mempool_try_free(&g_pool, obj));
}

/* mempool：HRT 级 ISR 中 reset 被拒绝（已分配槽位保持），普通路径可用 */
void test_mempool_reset_hrt_isr_rejected(void) {
    void *obj;

    (void)bm_mempool_try_free(&g_pool, bm_mempool_alloc(&g_pool));
    obj = bm_mempool_alloc(&g_pool);
    TEST_ASSERT_NOT_NULL(obj);

    bm_hrt_isr_enter();
    bm_mempool_reset(&g_pool);
    bm_hrt_isr_exit();

    /* reset 被拒绝：obj 槽位仍处已分配状态，可正常归还 */
    TEST_ASSERT_EQUAL(BM_OK, bm_mempool_try_free(&g_pool, obj));

    /* 普通路径 reset 生效：归还后 reset，随后可重新分配满池 */
    bm_mempool_reset(&g_pool);
    for (uint32_t i = 0u; i < 4u; ++i) {
        TEST_ASSERT_NOT_NULL(bm_mempool_alloc(&g_pool));
    }
    bm_mempool_reset(&g_pool);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_publish_from_below_threshold_isr_ok);
    RUN_TEST(test_publish_from_hrt_isr_rejected);
    RUN_TEST(test_publish_normal_path_ok);
    RUN_TEST(test_mempool_hrt_isr_rejected);
    RUN_TEST(test_mempool_reset_hrt_isr_rejected);
    return UNITY_END();
}
