/**
 * @file test_ipc.c
 * @brief 跨核 IPC seqlock 通道：功能往返 + 单写多读并发不变量
 *
 * seqlock（seq 奇偶 + release/acquire fence + CRC）是整个框架最危险的部分。
 * 本测试用框架自带的 bm_hal_cpu_boot_secondary 起 cpu1 做读者、cpu0（主线程）
 * 做唯一写者，符合 bm_ipc.h 声明的“单写者契约”，并在每次成功读时校验载荷
 * 内部不变量：撕裂读（torn read）必然破坏不变量，从而被捕获。
 *
 * 配合 CI 的 ThreadSanitizer 构建（native_mp 用真实 OS 线程），本测试可进一步
 * 暴露 release/acquire 内存序缺陷；本机无 sanitizer 时仍能捕获逻辑层撕裂。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            正式发布
 *
 */
#include "unity.h"
#include "bm/core/bm_ipc.h"
#include "bm/core/bm_boot.h"
#include "bm/common/bm_atomic_ipc.h"
#include "hal/bm_hal_cpu.h"
#include "bm_hal_cpu_mp_native.h"

#include <stdint.h>
#include <string.h>

/* 载荷内部不变量：由单一计数 v 派生三字段，读侧据此判定撕裂。 */
#define TEL_VOLTAGE(v) ((uint32_t)(v))
#define TEL_CURRENT(v) ((uint32_t)(v) ^ 0xA5A5A5A5u)
#define TEL_TEMP(v) ((uint32_t)(v) + 0x12340000u)

#define WRITER_ITERATIONS 200000u

static bm_ipc_shared_t s_ipc;
static bm_atomic_ipc_u32_t s_reader_done;

/* 读者侧统计；仅 cpu1 线程写，主线程在 join 之后读（join 建立 happens-before）。 */
static volatile uint32_t s_read_ok;
static volatile uint32_t s_read_torn;
static volatile uint32_t s_read_invalid;

static int tel_is_consistent(const bm_ipc_telemetry_t *t) {
    return (t->current_ma == TEL_CURRENT(t->voltage_mv) &&
            t->temperature_centi == TEL_TEMP(t->voltage_mv))
               ? 1
               : 0;
}

/* cpu1 入口：持续读取直到写者置位 done，再多读一轮排空尾包。 */
static void reader_entry(void) {
    uint32_t ok = 0u;
    uint32_t torn = 0u;
    uint32_t invalid = 0u;
    int draining = 0;

    for (;;) {
        bm_ipc_telemetry_t out;
        int rc = bm_ipc_read_telemetry_from(&s_ipc, 0u, &out);

        if (rc == BM_OK) {
            ok++;
            if (!tel_is_consistent(&out)) {
                torn++;
            }
        } else if (rc == BM_ERR_INVALID) {
            /* CRC 失配：seqlock 重检应已拦截并发写，此处不应出现。 */
            invalid++;
        }

        if (bm_atomic_ipc_load_u32(&s_reader_done) != 0u) {
            if (draining) {
                break;
            }
            draining = 1; /* 再走一轮，确保读到写者最后一次发布 */
        }
    }

    s_read_ok = ok;
    s_read_torn = torn;
    s_read_invalid = invalid;
}

void setUp(void) {
    bm_hal_cpu_init();
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(0u));
    TEST_ASSERT_EQUAL(BM_OK, bm_ipc_format(&s_ipc));
    bm_atomic_ipc_store_u32(&s_reader_done, 0u);
    s_read_ok = 0u;
    s_read_torn = 0u;
    s_read_invalid = 0u;
}

void tearDown(void) {
    (void)bm_hal_cpu_native_set_id(0u);
}

/* 单线程往返 + 序号去重：同一发布只被消费一次。 */
void test_ipc_telemetry_roundtrip_and_dedup(void) {
    bm_ipc_telemetry_t in = {
        .temperature_centi = TEL_TEMP(7u),
        .voltage_mv = TEL_VOLTAGE(7u),
        .current_ma = TEL_CURRENT(7u),
    };
    bm_ipc_telemetry_t out;

    /* 尚无发布：应阻塞。 */
    TEST_ASSERT_EQUAL(BM_ERR_WOULD_BLOCK,
                      bm_ipc_read_telemetry_from(&s_ipc, 0u, &out));

    TEST_ASSERT_EQUAL(BM_OK, bm_ipc_publish_telemetry(&s_ipc, &in));

    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(BM_OK, bm_ipc_read_telemetry_from(&s_ipc, 0u, &out));
    TEST_ASSERT_EQUAL_UINT32(in.voltage_mv, out.voltage_mv);
    TEST_ASSERT_EQUAL_UINT32(in.current_ma, out.current_ma);
    TEST_ASSERT_EQUAL_UINT32(in.temperature_centi, out.temperature_centi);
    TEST_ASSERT_TRUE(tel_is_consistent(&out));

    /* 无新发布：去重应再次阻塞。 */
    TEST_ASSERT_EQUAL(BM_ERR_WOULD_BLOCK,
                      bm_ipc_read_telemetry_from(&s_ipc, 0u, &out));
}

/* 单写（cpu0）多读（cpu1）：任意成功读的载荷必满足内部不变量。 */
void test_ipc_seqlock_single_writer_concurrent_reader(void) {
    uint32_t v;

    TEST_ASSERT_EQUAL(BM_OK,
                      bm_hal_cpu_boot_secondary((uintptr_t)reader_entry));

    for (v = 1u; v <= WRITER_ITERATIONS; v++) {
        bm_ipc_telemetry_t in = {
            .temperature_centi = TEL_TEMP(v),
            .voltage_mv = TEL_VOLTAGE(v),
            .current_ma = TEL_CURRENT(v),
        };
        TEST_ASSERT_EQUAL(BM_OK, bm_ipc_publish_telemetry(&s_ipc, &in));
        if ((v & 0x3FFu) == 0u) {
            bm_hal_cpu_yield(); /* 让读者线程获得调度，制造真实交错 */
        }
    }

    bm_atomic_ipc_store_u32(&s_reader_done, 1u);
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_join_secondary());

    /* 核心断言：从未观测到撕裂读，也从未观测到 CRC 失配。 */
    TEST_ASSERT_EQUAL_UINT32(0u, s_read_torn);
    TEST_ASSERT_EQUAL_UINT32(0u, s_read_invalid);
    /* 并发交错下读者应至少成功读到若干次，确保测试确有覆盖。 */
    TEST_ASSERT_GREATER_THAN_UINT32(0u, s_read_ok);
}

/* format / attach 的成功与失败路径。 */
void test_ipc_format_and_attach(void) {
    bm_ipc_shared_t *attached = NULL;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_ipc_format(NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_ipc_format(&s_ipc));

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_ipc_attach(NULL, (uintptr_t)&s_ipc));
    TEST_ASSERT_EQUAL(BM_OK, bm_ipc_attach(&attached, (uintptr_t)&s_ipc));
    TEST_ASSERT_EQUAL_PTR(&s_ipc, attached);

    /* magic 错配 */
    s_ipc.magic = 0xDEADBEEFu;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_ipc_attach(&attached, (uintptr_t)&s_ipc));
    /* layout 版本错配 */
    s_ipc.magic = BM_IPC_MAGIC;
    s_ipc.layout_version = BM_IPC_LAYOUT_VERSION + 1u;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_ipc_attach(&attached, (uintptr_t)&s_ipc));
}

/* 电平命令通道发布 + 按来源读取 + 去重。 */
void test_ipc_cmd_level_roundtrip_and_dedup(void) {
    bm_ipc_cmd_level_t in = {.opcode = 0x55u, .data = {1u, 2u, 3u}};
    bm_ipc_cmd_level_t out;

    TEST_ASSERT_EQUAL(BM_ERR_WOULD_BLOCK,
                      bm_ipc_read_cmd_level_from(&s_ipc, 0u, &out));
    TEST_ASSERT_EQUAL(BM_OK, bm_ipc_publish_cmd_level(&s_ipc, &in));

    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(BM_OK, bm_ipc_read_cmd_level_from(&s_ipc, 0u, &out));
    TEST_ASSERT_EQUAL_UINT32(in.opcode, out.opcode);
    TEST_ASSERT_EQUAL_UINT32(in.data[2], out.data[2]);

    TEST_ASSERT_EQUAL(BM_ERR_WOULD_BLOCK,
                      bm_ipc_read_cmd_level_from(&s_ipc, 0u, &out));
}

/* NULL 参数与越界来源的错误路径。 */
void test_ipc_null_and_range_errors(void) {
    bm_ipc_cmd_level_t cmd;
    bm_ipc_telemetry_t tel;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_ipc_publish_cmd_level(NULL, &cmd));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_ipc_publish_cmd_level(&s_ipc, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_ipc_publish_telemetry(NULL, &tel));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_ipc_publish_telemetry(&s_ipc, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_ipc_read_cmd_level_from(NULL, 0u, &cmd));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_ipc_read_cmd_level_from(&s_ipc, 0u, NULL));
    /* 越界来源 CPU */
    TEST_ASSERT_EQUAL(
        BM_ERR_INVALID,
        bm_ipc_read_cmd_level_from(&s_ipc, BM_CONFIG_CPU_COUNT, &cmd));
    TEST_ASSERT_EQUAL(
        BM_ERR_INVALID,
        bm_ipc_read_telemetry_from(&s_ipc, BM_CONFIG_CPU_COUNT, &tel));
}

#if BM_CONFIG_CPU_COUNT > 1u
/* 多核下默认来源读取与可靠命令环不被支持（须用 _from / IPC 矩阵）。 */
void test_ipc_default_and_rel_paths_unsupported_on_smp(void) {
    bm_ipc_cmd_level_t cmd;
    bm_ipc_telemetry_t tel;
    bm_ipc_rel_command_t rel = {.command_id = 1u, .opcode = 2u};
    bm_ipc_rel_command_t drained[BM_CONFIG_IPC_REL_CMD_CAPACITY];
    uint32_t count = 0u;

    TEST_ASSERT_EQUAL(BM_ERR_NOT_SUPPORTED,
                      bm_ipc_read_cmd_level(&s_ipc, &cmd));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_SUPPORTED,
                      bm_ipc_read_telemetry(&s_ipc, &tel));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_SUPPORTED,
                      bm_ipc_publish_cmd_rel(&s_ipc, &rel));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_SUPPORTED,
                      bm_ipc_drain_cmd_rel(&s_ipc,
                                           drained,
                                           BM_CONFIG_IPC_REL_CMD_CAPACITY,
                                           &count));
}
#endif

/* 心跳计数器递增、读取与 NULL 安全。 */
void test_ipc_heartbeats(void) {
    TEST_ASSERT_EQUAL_UINT32(0u, bm_ipc_read_rt_hb(&s_ipc));
    TEST_ASSERT_EQUAL_UINT32(0u, bm_ipc_read_srt_hb(&s_ipc));

    bm_ipc_bump_rt_hb(&s_ipc);
    bm_ipc_bump_rt_hb(&s_ipc);
    bm_ipc_bump_srt_hb(&s_ipc);
    TEST_ASSERT_EQUAL_UINT32(2u, bm_ipc_read_rt_hb(&s_ipc));
    TEST_ASSERT_EQUAL_UINT32(1u, bm_ipc_read_srt_hb(&s_ipc));

    /* NULL 安全：不崩溃，读返回 0 */
    bm_ipc_bump_rt_hb(NULL);
    bm_ipc_bump_srt_hb(NULL);
    TEST_ASSERT_EQUAL_UINT32(0u, bm_ipc_read_rt_hb(NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, bm_ipc_read_srt_hb(NULL));
}

/* RTD 启动状态机（bm_boot_*）：状态迁移与 NULL 安全。 */
void test_boot_state_machine(void) {
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_boot_format(NULL));
    TEST_ASSERT_EQUAL(BOOT_INIT, bm_boot_get_state(NULL));
    TEST_ASSERT_EQUAL(0, bm_boot_is_ready_for_irqs(NULL));

    TEST_ASSERT_EQUAL(BM_OK, bm_boot_format(&s_ipc));
    TEST_ASSERT_EQUAL(BOOT_INIT, bm_boot_get_state(&s_ipc));
    TEST_ASSERT_EQUAL(0, bm_boot_is_ready_for_irqs(&s_ipc));

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_boot_bootstrap_sequence(NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_boot_bootstrap_sequence(&s_ipc));
    TEST_ASSERT_EQUAL(BOOT_READY, bm_boot_get_state(&s_ipc));
    TEST_ASSERT_EQUAL(1, bm_boot_is_ready_for_irqs(&s_ipc));

    /* 从核序列：重新 format 后单独走 secondary 路径。 */
    TEST_ASSERT_EQUAL(BM_OK, bm_boot_format(&s_ipc));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_boot_secondary_sequence(NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_boot_secondary_sequence(&s_ipc));
    TEST_ASSERT_EQUAL(BOOT_READY, bm_boot_get_state(&s_ipc));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ipc_telemetry_roundtrip_and_dedup);
    RUN_TEST(test_boot_state_machine);
    RUN_TEST(test_ipc_seqlock_single_writer_concurrent_reader);
    RUN_TEST(test_ipc_format_and_attach);
    RUN_TEST(test_ipc_cmd_level_roundtrip_and_dedup);
    RUN_TEST(test_ipc_null_and_range_errors);
#if BM_CONFIG_CPU_COUNT > 1u
    RUN_TEST(test_ipc_default_and_rel_paths_unsupported_on_smp);
#endif
    RUN_TEST(test_ipc_heartbeats);
    return UNITY_END();
}
