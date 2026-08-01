/* SPDX-License-Identifier: LGPL-3.0-or-later */
/**
 * @file test_mask_hrt_guard.c
 * @brief 掩码模式（BM_CONFIG_ENABLE_PRIORITY_MASK=1）下 SRT 队列 API 对
 *        HRT 级 ISR 上下文 fail-closed 的运行期拦截单元测试
 *
 * 测试目标直接编译 Source/core/bm_event.c、bm_ultra.c 与 bm_mempool.c
 * （掩码宏开），临界区两层符号由 fakes/bm_mask_guard_fake.c 提供。断言：
 * - HRT ISR 上下文（bm_hrt_isr_enter 标记）中：
 *   bm_event_publish_copy_from_isr → BM_ERR_BUSY（且不触阈值掩码临界区）、
 *   bm_event_publish_copy（非 ISR 变体）同样 BM_ERR_BUSY、
 *   bm_ultra_queue_push/pop → BM_ERR_BUSY、bm_ultra_queue_reset 被拒绝、
 *   bm_mempool_alloc → NULL、bm_mempool_try_free → BM_ERR_BUSY、
 *   bm_mempool_reset 被拒绝（池内容不变）、
 *   bm_event_reset 静默拒绝（订阅与队列保持原状）、
 *   bm_event_process → BM_ERR_BUSY（队列不被触碰）；
 * - 低于 HRT 阈值的 from_isr 与普通主循环路径不受影响（BM_OK）；
 * - 正常入队确实以 BM_CONFIG_HRT_PRIORITY_THRESHOLD 进入阈值掩码临界区。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-08-01
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-07-30       1.0            zeh            正式发布
 * 2026-07-31       1.1            zeh            补非 ISR 变体与 ultra 队列拦截
 *                                                用例；setUp 复位内存池
 * 2026-07-31       1.2            zeh            补 bm_event_reset/process 拦截用例
 *                                                （对齐 ultra/mempool 的 reset 语义）
 * 2026-08-01       1.3            zeh            补完整事件发布入口的 HRT guard
 *                                                优先级用例
 */

#include "unity.h"
#include "bm/core/bm_event.h"
#include "bm/core/bm_mempool_impl.h"
#include "bm/common/bm_critical_wrap.h"
#include "bm/common/bm_types.h"
#include "bm_ultra.h"

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

/* 事件回调计数（event reset/process 拦截用例的观测点） */
static unsigned g_guard_cb_count;

static void guard_counting_cb(const bm_event_t *event, void *user_data) {
    (void)event;
    (void)user_data;
    g_guard_cb_count++;
}

/* bm_ultra.c 依赖应用侧实例化的回调表；本用例只验证入队拦截，全部留空 */
BM_ULTRA_CALLBACK_TABLE_DEFINE();

void setUp(void) {
    bm_mask_guard_fake_reset();
    bm_event_reset();
    g_guard_cb_count = 0u;
    /* 计数器跨用例必须保持平衡；此处防御性确认无残留 HRT 上下文 */
    TEST_ASSERT_EQUAL_INT(0, bm_in_hrt_isr());
    /* 池与 ultra 队列跨用例复位，消除用例间顺序耦合 */
    bm_mempool_reset(&g_pool);
    bm_ultra_queue_reset();
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

/*
 * HRT 回调里误用非 ISR 变体同样不安全（阈值掩码不互斥），拦截只看上下文，
 * 不看调用的是哪个变体
 */
void test_publish_normal_variant_in_hrt_context_rejected(void) {
    unsigned before;

    TEST_ASSERT_EQUAL(BM_OK, bm_event_register_type(GUARD_EVT_TYPE, "guard"));

    bm_hrt_isr_enter();
    before = bm_mask_guard_fake_enter_below_count();
    TEST_ASSERT_EQUAL(BM_ERR_BUSY,
        bm_event_publish_copy(GUARD_EVT_TYPE, GUARD_EVT_PRIO, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT(before, bm_mask_guard_fake_enter_below_count());
    bm_hrt_isr_exit();

    TEST_ASSERT_EQUAL(BM_OK,
        bm_event_publish_copy(GUARD_EVT_TYPE, GUARD_EVT_PRIO, NULL, 0u));
}

/**
 * @brief 完整事件发布入口须在参数与特性检查之前拒绝 HRT 级上下文
 */
void test_publish_event_variants_in_hrt_context_rejected_first(void) {
    bm_event_t event;
    unsigned before;

    event.type = GUARD_EVT_TYPE;
    event.priority = GUARD_EVT_PRIO;
    event.data_len = 0u;
    event.source_id = 0u;
    event.data = NULL;

    bm_hrt_isr_enter();
    before = bm_mask_guard_fake_enter_below_count();
    TEST_ASSERT_EQUAL(BM_ERR_BUSY, bm_event_publish_event(NULL));
    TEST_ASSERT_EQUAL(BM_ERR_BUSY, bm_event_publish_event_from_isr(NULL));
    TEST_ASSERT_EQUAL(BM_ERR_BUSY, bm_event_publish_event(&event));
    TEST_ASSERT_EQUAL(BM_ERR_BUSY, bm_event_publish_event_from_isr(&event));
    TEST_ASSERT_EQUAL_UINT(before, bm_mask_guard_fake_enter_below_count());
    bm_hrt_isr_exit();

    /* 退出 HRT 上下文后恢复默认特性语义，证明非 HRT 路径未被改写。 */
    TEST_ASSERT_EQUAL(BM_ERR_NOT_SUPPORTED, bm_event_publish_event(&event));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_SUPPORTED,
                      bm_event_publish_event_from_isr(&event));
}

/* ultra 队列：HRT 级上下文中 push/pop/reset 均 fail-closed */
void test_ultra_hrt_isr_rejected(void) {
    bm_ultra_queue_item_t item;
    bm_ultra_queue_item_t out;
    unsigned before;

    item.event_type = 0u;
    item.data_len = 0u;
    TEST_ASSERT_EQUAL(BM_OK, bm_ultra_queue_push(&item));
    TEST_ASSERT_EQUAL_UINT8(1u, bm_ultra_queue_count());

    bm_hrt_isr_enter();
    before = bm_mask_guard_fake_enter_below_count();
    TEST_ASSERT_EQUAL(BM_ERR_BUSY, bm_ultra_queue_push(&item));
    TEST_ASSERT_EQUAL(BM_ERR_BUSY, bm_ultra_queue_pop(&out));
    bm_ultra_queue_reset();
    /* 三次调用都在进入队列临界区之前被拦下 */
    TEST_ASSERT_EQUAL_UINT(before, bm_mask_guard_fake_enter_below_count());
    bm_hrt_isr_exit();

    /* reset 被拒绝：先前入队的项仍在，退出上下文后可正常弹出 */
    TEST_ASSERT_EQUAL_UINT8(1u, bm_ultra_queue_count());
    TEST_ASSERT_EQUAL(BM_OK, bm_ultra_queue_pop(&out));
    TEST_ASSERT_EQUAL_UINT8(0u, bm_ultra_queue_count());
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

/* event reset：HRT 级上下文中静默拒绝，订阅与队列保持原状 */
void test_event_reset_hrt_isr_rejected(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_event_register_type(GUARD_EVT_TYPE, "guard"));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_event_subscribe(GUARD_EVT_TYPE, guard_counting_cb, NULL, NULL));
    bm_event_freeze_subscriptions();
    TEST_ASSERT_EQUAL(BM_OK,
        bm_event_publish_copy(GUARD_EVT_TYPE, GUARD_EVT_PRIO, NULL, 0u));

    bm_hrt_isr_enter();
    bm_event_reset();
    bm_hrt_isr_exit();

    /* reset 被拒绝：订阅仍在且事件未丢，process 正常分发 */
    TEST_ASSERT_EQUAL(1, bm_event_process(4u));
    TEST_ASSERT_EQUAL_UINT(1u, g_guard_cb_count);
}

/* event process：HRT 级上下文中 fail-closed 返回 BM_ERR_BUSY，队列不被触碰 */
void test_event_process_hrt_isr_rejected(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_event_register_type(GUARD_EVT_TYPE, "guard"));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_event_subscribe(GUARD_EVT_TYPE, guard_counting_cb, NULL, NULL));
    bm_event_freeze_subscriptions();
    TEST_ASSERT_EQUAL(BM_OK,
        bm_event_publish_copy(GUARD_EVT_TYPE, GUARD_EVT_PRIO, NULL, 0u));

    bm_hrt_isr_enter();
    TEST_ASSERT_EQUAL(BM_ERR_BUSY, bm_event_process(4u));
    bm_hrt_isr_exit();

    /* 退出上下文后事件仍在队列中，可正常分发 */
    TEST_ASSERT_EQUAL(1, bm_event_process(4u));
    TEST_ASSERT_EQUAL_UINT(1u, g_guard_cb_count);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_publish_from_below_threshold_isr_ok);
    RUN_TEST(test_publish_from_hrt_isr_rejected);
    RUN_TEST(test_publish_normal_path_ok);
    RUN_TEST(test_publish_normal_variant_in_hrt_context_rejected);
    RUN_TEST(test_publish_event_variants_in_hrt_context_rejected_first);
    RUN_TEST(test_ultra_hrt_isr_rejected);
    RUN_TEST(test_mempool_hrt_isr_rejected);
    RUN_TEST(test_mempool_reset_hrt_isr_rejected);
    RUN_TEST(test_event_reset_hrt_isr_rejected);
    RUN_TEST(test_event_process_hrt_isr_rejected);
    return UNITY_END();
}
