/**
 * @file test_det_bounded.c
 * @brief L1 有界+非阻塞——C3 event process max_events 有界 + C4 mempool O(count) 有界
 *        + C5 atomic_inc 饱和语义（证明饱和上界，非 CAS 迭代次数）
 *
 * 缺口：
 *   C2 exec drain budget ← BM_ENABLE_EXEC_TEST_HOOK 未找到，待补。
 *   C5 CAS 迭代次数 ← 单核无 CAS 路径，多核通过静态审查（D3）证明。
 *   C6 ipc 非阻塞 ← BM_CONFIG_SMP=OFF，待 SMP 构建补入。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-30
 */
#include "unity.h"
#include "bm/core/bm_event.h"
#include "bm/core/bm_mempool.h"
#include "bm/common/bm_atomic.h"
#include "bm/common/bm_types.h"
#include <stdint.h>
#include <string.h>

/* ---- 静态资源 ---- */

/** @brief C4 mempool：16 个 uint32_t 槽 */
BM_MEMPOOL_DEFINE(det_bd_pool, uint32_t, 16u);

/** @brief C5 atomic 测试变量 */
static bm_atomic_t g_det_atomic_val;

/** @brief C3 用事件类型 ID（合法范围 [0,15]，取 13） */
#define DET_BD_EVT_TYPE ((bm_event_type_t)0x0Du)

void setUp(void)
{
    /* event 全局状态每 test 前复位 */
    bm_event_reset();
}

void tearDown(void) {}

/**
 * @brief C3：event process 的实际处理数 <= max_events（有界迭代验证）
 *
 * 通过 BM_ENABLE_EVENT_TEST_HOOK 提供的 bm_event_test_inject 向队列注入
 * 3 条事件（绕过类型注册，直接进队；priority 0 环形有效容量 = 16/4 - 1 = 3），
 * 冻结订阅表后（bm_event_process 的确定性前置条件）再以 max_events=2 调用
 * bm_event_process，断言返回值 <= 2，证明 process 在 max_events 上界处截止
 * （spec §4.1 有界迭代）。
 *
 * 注：inject 注入的事件 type=DET_BD_EVT_TYPE 未注册，dispatch 时 head=NULL 不分发，
 * 但 process 每出队一条仍计数，故"处理条数"包含跳过的条目，在不超 max_events 的
 * 前提下仍能证明循环有界。非空洞证据：r1<=2 且 r1+r2==3。
 */
void test_det_bounded_event_process_max_events(void)
{
#ifdef BM_ENABLE_EVENT_TEST_HOOK
    bm_event_t ev;
    int        r1;
    int        r2;
    uint32_t   i;

    memset(&ev, 0, sizeof(ev));
    ev.type       = DET_BD_EVT_TYPE;
    ev.priority   = 0u;
    ev.data_len   = 0u;
    ev.source_id  = 0u;
    ev.data       = NULL;

    /* 队列每优先级环形有效容量 = BM_CONFIG_EVENT_QUEUE_SIZE/BM_CONFIG_EVENT_PRIORITIES - 1
       = 16/4 - 1 = 3；priority 0 最多积 3 条，注入 3 条不溢出 */
    for (i = 0u; i < 3u; i++) {
        TEST_ASSERT_EQUAL(BM_OK, bm_event_test_inject(&ev, 0u));
    }

    /* 确定性流式防护：bm_event_process 仅在订阅表冻结后才分发，否则返回
       BM_ERR_NOT_INIT；本测试无订阅，冻结后直接进入有界出队循环 */
    bm_event_freeze_subscriptions();

    /* 队列有 3 条 > max_events=2：process 必须在 max_events 上界处截止，返回值 <= 2 */
    r1 = bm_event_process(2u);
    TEST_ASSERT_TRUE(r1 >= 0);
    TEST_ASSERT_TRUE((uint32_t)r1 <= 2u);

    /* 处理剩余：max_events=10 但队列仅剩 1 条 */
    r2 = bm_event_process(10u);
    TEST_ASSERT_TRUE(r2 >= 0);
    TEST_ASSERT_TRUE((uint32_t)r2 <= 10u);

    /* 非空洞证据：3 条全部被处理，且首次被 max_events=2 截断 ⇒
       r1<=2 且 r1+r2==3，证明上界确实生效（而非队列耗尽提前返回） */
    TEST_ASSERT_EQUAL_INT(3, r1 + r2);
#else
    /* 未启用 TEST_HOOK 时跳过（ctest 不计为失败） */
    TEST_IGNORE_MESSAGE("BM_ENABLE_EVENT_TEST_HOOK not defined; C3 bounded test skipped");
#endif
}

/**
 * @brief C4：mempool 分配扫描 O(count) 有界——池满后 alloc 返回 NULL，不无限自旋
 *
 * 直接填满 16 槽，第 17 次 alloc 必须返回 NULL（有界位图扫描），
 * 不会挂死。随后 free+alloc 验证归还逻辑同样有界。
 * （bm_mempool_alloc 位图扫描界 = count，最坏情况 O(count)，spec §4.1 有界迭代）
 */
void test_det_bounded_mempool_full_then_null(void)
{
    void    *ptrs[16u];
    void    *extra;
    uint32_t i;

    bm_mempool_reset(&det_bd_pool);

    /* 填满 16 槽，全部应成功 */
    for (i = 0u; i < 16u; i++) {
        ptrs[i] = bm_mempool_alloc(&det_bd_pool);
        TEST_ASSERT_NOT_NULL(ptrs[i]);
    }

    /* 池满：第 17 次 alloc 返回 NULL（有界扫描完整 16 槽后放弃） */
    extra = bm_mempool_alloc(&det_bd_pool);
    TEST_ASSERT_NULL(extra);

    /* free 中间一槽，再 alloc 应成功（free + 再分配也是 O(count)） */
    bm_mempool_free(&det_bd_pool, ptrs[7u]);
    ptrs[7u] = bm_mempool_alloc(&det_bd_pool);
    TEST_ASSERT_NOT_NULL(ptrs[7u]);

    /* 归还全部，复位后 alloc 应重新成功（幂等验证） */
    for (i = 0u; i < 16u; i++) {
        bm_mempool_free(&det_bd_pool, ptrs[i]);
    }
    bm_mempool_reset(&det_bd_pool);
    ptrs[0] = bm_mempool_alloc(&det_bd_pool);
    TEST_ASSERT_NOT_NULL(ptrs[0]);
    bm_mempool_free(&det_bd_pool, ptrs[0]);
}

/**
 * @brief C5：bm_atomic_inc 饱和语义（证明上界为 UINT32_MAX，不回绕）
 *
 * 预设计数器为 UINT32_MAX-1，连续 inc 两次：
 *   - 第 1 次：UINT32_MAX-1 → UINT32_MAX（正常递增）
 *   - 第 2 次：饱和，仍返回 UINT32_MAX（不回绕到 0）
 * 证明饱和上界存在（bm_atomic.h:69 文档：UINT32_MAX 处饱和）。
 *
 * 注：多核 CAS 路径（BM_CPU_LOCAL_ENABLE_ROUTE）的有界迭代次数由
 * BM_CONFIG_ATOMIC_MAX_RETRIES=8（bm_config.h:346）静态保证，
 * 通过确定性审查报告 D3/D6 证明，此处不重复实测（单核 native 无 CAS）。
 */
void test_det_bounded_atomic_inc_saturation(void)
{
    uint32_t v;

    bm_atomic_store(&g_det_atomic_val, UINT32_MAX - 1u);

    /* 第 1 次 inc：UINT32_MAX-1 → UINT32_MAX */
    v = bm_atomic_inc(&g_det_atomic_val);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, v);

    /* 第 2 次 inc：已在 UINT32_MAX，饱和不回绕 */
    v = bm_atomic_inc(&g_det_atomic_val);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, v);

    /* 当前存储值仍为 UINT32_MAX */
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, bm_atomic_load(&g_det_atomic_val));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_det_bounded_event_process_max_events);
    RUN_TEST(test_det_bounded_mempool_full_then_null);
    RUN_TEST(test_det_bounded_atomic_inc_saturation);
    return UNITY_END();
}
