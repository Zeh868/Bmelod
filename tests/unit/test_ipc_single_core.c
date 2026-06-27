/**
 * @file test_ipc_single_core.c
 * @brief 单核（CPU_COUNT=1）IPC 路径单元测试
 *
 * 多核构建（CPU_COUNT=2）会把可靠命令环与默认来源读取编译为 NOT_SUPPORTED，
 * 因此这些单核分支在常规测试里覆盖率为 0。本测试以 CPU_COUNT=1 独立编译
 * Source/core/bm_ipc.c，专门覆盖：
 *   - 可靠命令环 publish / drain / 溢出 / 索引回绕 / 损坏检测；
 *   - 默认来源 read_cmd_level / read_telemetry；
 *   - 电平命令与遥测往返。
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

#include <stdint.h>
#include <string.h>

/* 单核 HAL 桩：bm_ipc.c 的读路径调用 bm_hal_cpu_id()。 */
uint32_t bm_hal_cpu_id(void) {
    return 0u;
}

static bm_ipc_shared_t s_ipc;

void setUp(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_ipc_format(&s_ipc));
}

void tearDown(void) {
}

/* 默认来源读取在单核下走 _from(0) 实路径。 */
void test_sc_cmd_and_telemetry_default_roundtrip(void) {
    bm_ipc_cmd_level_t cmd_in = {.opcode = 0xABu, .data = {9u, 8u, 7u}};
    bm_ipc_cmd_level_t cmd_out;
    bm_ipc_telemetry_t tel_in = {.temperature_centi = 2500u,
                                 .voltage_mv = 3300u,
                                 .current_ma = 120u};
    bm_ipc_telemetry_t tel_out;

    TEST_ASSERT_EQUAL(BM_ERR_WOULD_BLOCK,
                      bm_ipc_read_cmd_level(&s_ipc, &cmd_out));
    TEST_ASSERT_EQUAL(BM_OK, bm_ipc_publish_cmd_level(&s_ipc, &cmd_in));
    TEST_ASSERT_EQUAL(BM_OK, bm_ipc_read_cmd_level(&s_ipc, &cmd_out));
    TEST_ASSERT_EQUAL_UINT32(cmd_in.opcode, cmd_out.opcode);
    TEST_ASSERT_EQUAL(BM_ERR_WOULD_BLOCK,
                      bm_ipc_read_cmd_level(&s_ipc, &cmd_out));

    TEST_ASSERT_EQUAL(BM_OK, bm_ipc_publish_telemetry(&s_ipc, &tel_in));
    TEST_ASSERT_EQUAL(BM_OK, bm_ipc_read_telemetry(&s_ipc, &tel_out));
    TEST_ASSERT_EQUAL_UINT32(tel_in.voltage_mv, tel_out.voltage_mv);
}

/* 可靠命令环：FIFO publish/drain。 */
void test_sc_rel_cmd_fifo(void) {
    bm_ipc_rel_command_t out[BM_CONFIG_IPC_REL_CMD_CAPACITY];
    uint32_t count = 0u;
    uint32_t i;

    for (i = 0u; i < BM_CONFIG_IPC_REL_CMD_CAPACITY; i++) {
        bm_ipc_rel_command_t cmd = {.command_id = i + 1u, .opcode = i + 100u};
        TEST_ASSERT_EQUAL(BM_OK, bm_ipc_publish_cmd_rel(&s_ipc, &cmd));
    }
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_ipc_drain_cmd_rel(&s_ipc,
                                           out,
                                           BM_CONFIG_IPC_REL_CMD_CAPACITY,
                                           &count));
    TEST_ASSERT_EQUAL_UINT32(BM_CONFIG_IPC_REL_CMD_CAPACITY, count);
    for (i = 0u; i < count; i++) {
        TEST_ASSERT_EQUAL_UINT32(i + 1u, out[i].command_id);
        TEST_ASSERT_EQUAL_UINT32(i + 100u, out[i].opcode);
    }
}

/* 环满时 publish 返回 OVERFLOW。 */
void test_sc_rel_cmd_overflow(void) {
    bm_ipc_rel_command_t cmd = {.command_id = 1u, .opcode = 2u};
    uint32_t i;

    for (i = 0u; i < BM_CONFIG_IPC_REL_CMD_CAPACITY; i++) {
        TEST_ASSERT_EQUAL(BM_OK, bm_ipc_publish_cmd_rel(&s_ipc, &cmd));
    }
    TEST_ASSERT_EQUAL(BM_ERR_OVERFLOW, bm_ipc_publish_cmd_rel(&s_ipc, &cmd));
}

/* 索引回绕：跨越 capacity 边界反复 publish/drain，head/tail 持续累加。 */
void test_sc_rel_cmd_index_wrap(void) {
    bm_ipc_rel_command_t out[1];
    uint32_t count;
    uint32_t round;

    for (round = 0u; round < BM_CONFIG_IPC_REL_CMD_CAPACITY * 3u; round++) {
        bm_ipc_rel_command_t cmd = {.command_id = round + 1u, .opcode = round};
        count = 0u;
        TEST_ASSERT_EQUAL(BM_OK, bm_ipc_publish_cmd_rel(&s_ipc, &cmd));
        TEST_ASSERT_EQUAL(BM_OK, bm_ipc_drain_cmd_rel(&s_ipc, out, 1u, &count));
        TEST_ASSERT_EQUAL_UINT32(1u, count);
        TEST_ASSERT_EQUAL_UINT32(round + 1u, out[0].command_id);
    }
}

/* drain 空环返回 OK / count=0；capacity=0 不消费。 */
void test_sc_rel_cmd_drain_empty(void) {
    bm_ipc_rel_command_t out[1];
    uint32_t count = 123u;

    TEST_ASSERT_EQUAL(BM_OK, bm_ipc_drain_cmd_rel(&s_ipc, out, 1u, &count));
    TEST_ASSERT_EQUAL_UINT32(0u, count);

    count = 123u;
    TEST_ASSERT_EQUAL(BM_OK, bm_ipc_drain_cmd_rel(&s_ipc, NULL, 0u, &count));
    TEST_ASSERT_EQUAL_UINT32(0u, count);
}

/* drain 检测 head/tail 损坏（pending > capacity）。 */
void test_sc_rel_cmd_drain_detects_corruption(void) {
    bm_ipc_rel_command_t out[BM_CONFIG_IPC_REL_CMD_CAPACITY];
    uint32_t count = 0u;

    /* 人为制造 pending 超过容量的非法状态 */
    bm_atomic_ipc_store_u32(&s_ipc.rel_cmd.head,
                            BM_CONFIG_IPC_REL_CMD_CAPACITY + 5u);
    bm_atomic_ipc_store_u32(&s_ipc.rel_cmd.tail, 0u);

    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_ipc_drain_cmd_rel(&s_ipc,
                                           out,
                                           BM_CONFIG_IPC_REL_CMD_CAPACITY,
                                           &count));
}

/* NULL 参数错误路径。 */
void test_sc_rel_cmd_null_args(void) {
    bm_ipc_rel_command_t cmd = {.command_id = 1u, .opcode = 2u};
    bm_ipc_rel_command_t out[1];
    uint32_t count = 0u;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_ipc_publish_cmd_rel(NULL, &cmd));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_ipc_publish_cmd_rel(&s_ipc, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_ipc_drain_cmd_rel(NULL, out, 1u, &count));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_ipc_drain_cmd_rel(&s_ipc, NULL, 1u, &count));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_ipc_drain_cmd_rel(&s_ipc, out, 1u, NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_sc_cmd_and_telemetry_default_roundtrip);
    RUN_TEST(test_sc_rel_cmd_fifo);
    RUN_TEST(test_sc_rel_cmd_overflow);
    RUN_TEST(test_sc_rel_cmd_index_wrap);
    RUN_TEST(test_sc_rel_cmd_drain_empty);
    RUN_TEST(test_sc_rel_cmd_drain_detects_corruption);
    RUN_TEST(test_sc_rel_cmd_null_args);
    return UNITY_END();
}
