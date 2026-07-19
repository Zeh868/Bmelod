/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_tt_schedule_json.c
 * @brief bm_tt_schedule_report_json 单元测试（schedule-map v2 Task 2，schema v1）
 *
 * @details 搭建一张谐波周期调度表（fast every=1 at=0 wcet=50 / mid
 * every=5 at=0 wcet=50 / slow every=10 at=9 wcet=50，均为 ISR 域；tele
 * every=10 at=0 wcet=200，MAINLOOP 域；minor_us=1000，
 * n_frames=LCM(1,5,10,10)=10），装配风格与
 * `tests/tools/tt_schedule_map_dump.c`、`tests/unit/test_tt_schedule.c`
 * 场景 11 一致（同用 BM_BUS_DEFINE/BM_LET_DEFINE_ISR/
 * BM_LET_DEFINE_MAINLOOP/BM_SCHEDULE_DEFINE）。五个用例：
 *   1. 显式传入 meta（cpu/ref_clk_hz/operating_points_hz）时的关键事实——
 *      断言 schema_version/n_frames/hyperperiod_us/ref_clk_hz/
 *      operating_points_hz，以及第 0 帧、第 9 帧的准确负载行
 *      （isr_load_us/mainloop_pending_us），加上预留的空 edges 数组。
 *   2. meta == NULL 时退化为全零默认值（cpu=0/ref_clk_hz=0/无工作点/
 *      interference_sources 为空数组）。
 *   3. Task 5：meta 带 2 个干扰源时 interference_sources 一源一行导出，
 *      tier 字段导出为 "hardware"/"scheduled" 字符串。
 *   4. 回归：operating_point_count 足够多（40 个）时 operating_points_hz
 *      单行拼装不越界——逐行 strlen 必须 < 200（TT_REPORT_LINE_MAX）。
 *      本机 mingw 的 snprintf 是 msvcrt 语义，截断时返回 -1 且不补 NUL，
 *      旧实现假设截断后仍 NUL 结尾，40 个工作点填满行缓冲时该假设不成立。
 *   5. 确定性：同一调度表输出两次，结果逐字节相同。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-04
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-03       1.0            zeh            Task 2：JSON 导出初版测试
 * 2026-07-03       1.1            zeh            新增 40 工作点越界回归用例，
 *                                                 回归 operating_points_hz 单行拼装
 *                                                 越界读缺陷
 * 2026-07-04       1.2            zeh            Task 5：新增 interference_sources
 *                                                 用例（1~2 干扰源一源一行 + 空数组
 *                                                 默认回退），覆盖 meta 新增
 *                                                 interference/interference_count 字段
 *
 */
#include "unity.h"
#include "bm_tt_schedule.h"
#include "bm_bus.h"

#include <string.h>

/* =========================================================================
 * 装配件：谐波周期调度表（fast/mid/slow ISR + tele MAINLOOP）
 * ========================================================================= */

BM_BUS_DEFINE(json_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(json_fast_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(json_mid_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(json_slow_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(json_tele_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_json_in_bus;
static bm_bus_t g_json_fast_out_bus;
static bm_bus_t g_json_mid_out_bus;
static bm_bus_t g_json_slow_out_bus;
static bm_bus_t g_json_tele_out_bus;

static const uint32_t k_json_in_safe = 0u;
static const uint32_t k_json_out_safe = 0u;

/** @brief 空操作 step：本测试组只考察 bm_tt_schedule_report_json 的文本输出 */
static void json_noop_step(bm_let_ctx_t *ctx, void *state) {
    (void)ctx;
    (void)state;
}

static const bm_let_input_t k_json_inputs[] = {
    { .bus = &g_json_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_json_in_safe },
};
static const bm_let_output_t k_json_fast_outputs[] = {
    { .bus = &g_json_fast_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_json_out_safe },
};
static const bm_let_output_t k_json_mid_outputs[] = {
    { .bus = &g_json_mid_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_json_out_safe },
};
static const bm_let_output_t k_json_slow_outputs[] = {
    { .bus = &g_json_slow_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_json_out_safe },
};
static const bm_let_output_t k_json_tele_outputs[] = {
    { .bus = &g_json_tele_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_json_out_safe },
};

BM_LET_DEFINE_ISR(task_json_fast, 1u, 0u, 50u, json_noop_step, NULL,
                   k_json_inputs, k_json_fast_outputs);
BM_LET_DEFINE_ISR(task_json_mid, 5u, 0u, 50u, json_noop_step, NULL,
                   k_json_inputs, k_json_mid_outputs);
BM_LET_DEFINE_ISR(task_json_slow, 10u, 9u, 50u, json_noop_step, NULL,
                   k_json_inputs, k_json_slow_outputs);
BM_LET_DEFINE_MAINLOOP(task_json_tele, 10u, 0u, 200u, json_noop_step, NULL,
                        k_json_inputs, k_json_tele_outputs);
BM_SCHEDULE_DEFINE(sched_json, 1000u, &task_json_fast, &task_json_mid,
                    &task_json_slow, &task_json_tele);

/** @brief 测试用例 1 的 meta 所用工作点频率数组 */
static const uint32_t k_json_ops[] = { 240000000u, 80000000u };

/* =========================================================================
 * 逐行捕获辅助函数
 * ========================================================================= */

static char g_json_buf[8192];

/** @brief emit 回调：把每行 + '\n' 追加进静态捕获缓冲 g_json_buf */
static void json_emit(const char *line, void *u) {
    (void)u;
    (void)strncat(g_json_buf, line, sizeof(g_json_buf) - strlen(g_json_buf) - 2u);
    (void)strncat(g_json_buf, "\n", sizeof(g_json_buf) - strlen(g_json_buf) - 1u);
}

/** @brief Unity 每用例钩子（本文件未用：各测试自行管理 bus 的 open/close） */
void setUp(void) {
}

/** @brief Unity 每用例钩子（本文件未用：各测试自行管理 bus 的 open/close） */
void tearDown(void) {
}

/**
 * @brief 用例 1：显式传入 meta -> 断言关键 schema 字段、逐任务事实与逐帧负载
 */
void test_report_json_key_facts_with_explicit_meta(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    bm_tt_schedule_json_meta_t meta = { 0u, 240000000u, k_json_ops, 2u };

    g_json_buf[0] = '\0';

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_in_bus, &json_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_fast_out_bus, &json_fast_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_mid_out_bus, &json_mid_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_slow_out_bus, &json_slow_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_tele_out_bus, &json_tele_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_tt_schedule_init(&sched_json));

    bm_tt_schedule_report_json(&sched_json, &meta, json_emit, NULL);

    TEST_ASSERT_NOT_NULL(strstr(g_json_buf, "\"schema_version\": 1"));
    TEST_ASSERT_NOT_NULL(strstr(g_json_buf, "\"n_frames\": 10"));
    TEST_ASSERT_NOT_NULL(strstr(g_json_buf, "\"hyperperiod_us\": 10000"));
    TEST_ASSERT_NOT_NULL(strstr(g_json_buf, "\"ref_clk_hz\": 240000000"));
    TEST_ASSERT_NOT_NULL(strstr(g_json_buf, "\"operating_points_hz\": [240000000, 80000000]"));
    TEST_ASSERT_NOT_NULL(strstr(g_json_buf,
        "{\"t\": 0, \"isr_load_us\": 100, \"mainloop_pending_us\": 200}"));
    TEST_ASSERT_NOT_NULL(strstr(g_json_buf,
        "{\"t\": 9, \"isr_load_us\": 100, \"mainloop_pending_us\": 0}"));
    TEST_ASSERT_NOT_NULL(strstr(g_json_buf, "\"edges\": []"));

    bm_bus_close(&g_json_in_bus);
    bm_bus_close(&g_json_fast_out_bus);
    bm_bus_close(&g_json_mid_out_bus);
    bm_bus_close(&g_json_slow_out_bus);
    bm_bus_close(&g_json_tele_out_bus);
}

/**
 * @brief 用例 2：meta == NULL 退化为全零默认值
 */
void test_report_json_null_meta_defaults_to_zero(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };

    g_json_buf[0] = '\0';

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_in_bus, &json_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_fast_out_bus, &json_fast_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_mid_out_bus, &json_mid_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_slow_out_bus, &json_slow_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_tele_out_bus, &json_tele_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_tt_schedule_init(&sched_json));

    bm_tt_schedule_report_json(&sched_json, NULL, json_emit, NULL);

    TEST_ASSERT_NOT_NULL(strstr(g_json_buf, "\"cpu\": 0"));
    TEST_ASSERT_NOT_NULL(strstr(g_json_buf, "\"ref_clk_hz\": 0"));
    TEST_ASSERT_NOT_NULL(strstr(g_json_buf, "\"operating_points_hz\": []"));
    TEST_ASSERT_NOT_NULL(strstr(g_json_buf, "\"interference_sources\": []"));

    bm_bus_close(&g_json_in_bus);
    bm_bus_close(&g_json_fast_out_bus);
    bm_bus_close(&g_json_mid_out_bus);
    bm_bus_close(&g_json_slow_out_bus);
    bm_bus_close(&g_json_tele_out_bus);
}

/** @brief 用例 5 专用：2 个干扰源（1 硬件 tier + 1 已调度 tier），
 *  覆盖 bm_tt_schedule_report_json 新增的 interference_sources 一源一行发射 */
static const bm_tt_sched_intf_src_t k_json_intf_srcs[] = {
    { "spi_isr", 1000u, 20u, 0u },
    { "wifi_task", 5000u, 300u, 1u },
};

/**
 * @brief 用例 5：meta 带 2 个干扰源 -> JSON 含 interference_sources 数组，
 * 逐源一行、tier 导出为 "hardware"/"scheduled" 字符串
 */
void test_report_json_interference_sources_with_meta(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    bm_tt_schedule_json_meta_t meta = {
        .cpu = 0u,
        .ref_clk_hz = 240000000u,
        .operating_points_hz = k_json_ops,
        .operating_point_count = 2u,
        .interference = k_json_intf_srcs,
        .interference_count = 2u,
    };

    g_json_buf[0] = '\0';

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_in_bus, &json_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_fast_out_bus, &json_fast_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_mid_out_bus, &json_mid_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_slow_out_bus, &json_slow_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_tele_out_bus, &json_tele_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_tt_schedule_init(&sched_json));

    bm_tt_schedule_report_json(&sched_json, &meta, json_emit, NULL);

    TEST_ASSERT_NOT_NULL(strstr(g_json_buf, "\"interference_sources\": ["));
    TEST_ASSERT_NOT_NULL(strstr(g_json_buf,
        "{\"name\": \"spi_isr\", \"period_us\": 1000, \"wcet_us\": 20, \"tier\": \"hardware\"},"));
    TEST_ASSERT_NOT_NULL(strstr(g_json_buf,
        "{\"name\": \"wifi_task\", \"period_us\": 5000, \"wcet_us\": 300, \"tier\": \"scheduled\"}"));

    bm_bus_close(&g_json_in_bus);
    bm_bus_close(&g_json_fast_out_bus);
    bm_bus_close(&g_json_mid_out_bus);
    bm_bus_close(&g_json_slow_out_bus);
    bm_bus_close(&g_json_tele_out_bus);
}

/** @brief 用例 4 专用：40 个工作点频率（放大值，逼近/超过单行栈缓冲上界） */
static const uint32_t k_json_many_ops[40] = {
    100000000u, 100000001u, 100000002u, 100000003u, 100000004u,
    100000005u, 100000006u, 100000007u, 100000008u, 100000009u,
    100000010u, 100000011u, 100000012u, 100000013u, 100000014u,
    100000015u, 100000016u, 100000017u, 100000018u, 100000019u,
    100000020u, 100000021u, 100000022u, 100000023u, 100000024u,
    100000025u, 100000026u, 100000027u, 100000028u, 100000029u,
    100000030u, 100000031u, 100000032u, 100000033u, 100000034u,
    100000035u, 100000036u, 100000037u, 100000038u, 100000039u,
};

/** @brief 用例 4 专用：逐行捕获时观察到的最长行长度（含 NUL 前 strlen） */
static size_t g_json_max_line_len = 0u;

/** @brief 用例 4 专用：本次 emit 调用序列中是否出现越界行（strlen 达到/超过
 *  TT_REPORT_LINE_MAX，即 bm_tt_schedule.c 中定长栈缓冲 `char line[200]` 的容量——
 *  该常量在被测源文件里叫 TT_REPORT_LINE_MAX，此处按其字面值 200 复刻，
 *  避免为测试而 #include 被测 .c 的私有宏）*/
#define TEST_TT_REPORT_LINE_MAX 200u

/** @brief 用例 4 专用 emit 回调：不拼接大缓冲，逐行单独 strlen 校验/记录，
 *  避免修复前的越界读进一步污染共享静态缓冲 g_json_buf */
static void json_emit_check_line_bound(const char *line, void *u) {
    size_t len;

    (void)u;
    len = strlen(line); /* 若 line 未在 200 字节内 NUL 终止，此处即读越界 */
    if (len > g_json_max_line_len) {
        g_json_max_line_len = len;
    }
}

/**
 * @brief 用例 4：operating_point_count 足够多（40 个）时，operating_points_hz
 * 单行拼装不得越界——每一行 strlen 必须 < TT_REPORT_LINE_MAX(200)。
 *
 * @details 回归 Critical 缺陷：本机 mingw 的 snprintf 是 msvcrt 语义，截断时
 * 返回 -1 且不补 NUL 终止符。旧实现用返回值累加 off、假设截断后仍 NUL 结尾，
 * 40 个工作点填满 200 字节栈缓冲时该假设不成立，line 可能无界内 NUL，
 * 经 strlen/emit 读越界。修复后无论 snprintf 截断与否，line 都强制在
 * sizeof line - 1 处补 NUL，故此处每行 strlen 恒 < 200。
 */
void test_report_json_many_operating_points_no_line_overflow(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    bm_tt_schedule_json_meta_t meta = { 0u, 240000000u, k_json_many_ops, 40u };

    g_json_max_line_len = 0u;

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_in_bus, &json_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_fast_out_bus, &json_fast_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_mid_out_bus, &json_mid_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_slow_out_bus, &json_slow_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_tele_out_bus, &json_tele_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_tt_schedule_init(&sched_json));

    bm_tt_schedule_report_json(&sched_json, &meta, json_emit_check_line_bound, NULL);

    TEST_ASSERT_TRUE_MESSAGE(g_json_max_line_len < TEST_TT_REPORT_LINE_MAX,
        "operating_points_hz 单行拼装越界：某行 strlen 达到/超过 200 字节栈缓冲上界");

    bm_bus_close(&g_json_in_bus);
    bm_bus_close(&g_json_fast_out_bus);
    bm_bus_close(&g_json_mid_out_bus);
    bm_bus_close(&g_json_slow_out_bus);
    bm_bus_close(&g_json_tele_out_bus);
}

/**
 * @brief 用例 3：确定性 -- 同一调度表输出两次结果相同
 */
void test_report_json_deterministic_across_two_runs(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    bm_tt_schedule_json_meta_t meta = { 0u, 240000000u, k_json_ops, 2u };
    static char buf_a[8192];
    static char buf_b[8192];

    buf_a[0] = '\0';
    buf_b[0] = '\0';

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_in_bus, &json_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_fast_out_bus, &json_fast_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_mid_out_bus, &json_mid_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_slow_out_bus, &json_slow_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_tele_out_bus, &json_tele_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_tt_schedule_init(&sched_json));

    /* json_emit 无论 u 为何总是追加到共享的 g_json_buf；每次运行前后各重置一次，
     * 并在比较两次运行前把结果快照进各自的缓冲区。 */
    g_json_buf[0] = '\0';
    bm_tt_schedule_report_json(&sched_json, &meta, json_emit, NULL);
    (void)strncpy(buf_a, g_json_buf, sizeof(buf_a) - 1u);
    buf_a[sizeof(buf_a) - 1u] = '\0';

    g_json_buf[0] = '\0';
    bm_tt_schedule_report_json(&sched_json, &meta, json_emit, NULL);
    (void)strncpy(buf_b, g_json_buf, sizeof(buf_b) - 1u);
    buf_b[sizeof(buf_b) - 1u] = '\0';

    TEST_ASSERT_EQUAL_STRING(buf_a, buf_b);

    bm_bus_close(&g_json_in_bus);
    bm_bus_close(&g_json_fast_out_bus);
    bm_bus_close(&g_json_mid_out_bus);
    bm_bus_close(&g_json_slow_out_bus);
    bm_bus_close(&g_json_tele_out_bus);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_report_json_key_facts_with_explicit_meta);
    RUN_TEST(test_report_json_null_meta_defaults_to_zero);
    RUN_TEST(test_report_json_interference_sources_with_meta);
    RUN_TEST(test_report_json_many_operating_points_no_line_overflow);
    RUN_TEST(test_report_json_deterministic_across_two_runs);
    return UNITY_END();
}
