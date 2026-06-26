/**
 * @file test_shell_cmds.c
 * @brief bus_servo shell 业务命令单元测试（路线图 #11 shell 业务命令集成）
 *
 * 测试「命令字符串 → 业务动作生效」的完整链路：
 *   1. shell_cmds_register 成功注册三条命令
 *   2. "stats" 命令：有无写入数据均正常返回 BM_OK，不崩溃
 *   3. "set 3.5"：cmd_bus 上写入 velocity_setpoint_rad_s=3.5、ENABLED 位置位
 *   4. "set"（缺参数）：返回 BM_ERR_INVALID
 *   5. "set abc"（非法浮点）：返回 BM_ERR_INVALID
 *   6. "set -1.0"（负速度）：接受，velocity_setpoint_rad_s=-1.0
 *   7. "fault"：cmd_bus 上写入 FAULT 位置位、速度清零的命令
 *
 * @note 测试用 bus 由 setUp 直接构造（BM_BUS_DEFINE + bm_bus_open + reader_attach +
 *       freeze），无需启动完整 demo；shell_cmds.c 不依赖 demo 全局变量，
 *       可独立编译并注入测试专用 bus 句柄。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-26       1.0            zeh            初稿：#11 shell 业务命令测试
 *
 */

#include "unity.h"
#include "shell_cmds.h"
#include "app_bus_servo.h"   /* bus_servo_command_t, BUS_SERVO_CMD_STATUS_* */
#include "bm_shell.h"
#include "bm_bus.h"
#include "bm_log.h"

#include <string.h>

/* =========================================================================
 * 测试用 bus 静态存储
 *
 * 每次 setUp 通过 bm_bus_open 重置运行期状态，静态存储只初始化一次，
 * 与 test_health.c / test_bus.c 中的惯例保持一致。
 * ========================================================================= */

/** 测试用 QUEUE 命令 bus（容量 4，1 读者，与 demo 相同参数） */
BM_BUS_DEFINE(sc_cmd_bus, bus_servo_command_t, 4u, 1u, BM_BUS_QUEUE);
/** 测试用 QUEUE 遥测 bus（仅供 stats 打印统计，不验证消费内容） */
BM_BUS_DEFINE(sc_telem_bus, bus_servo_command_t, 4u, 1u, BM_BUS_QUEUE);

/** 测试用 cmd_bus 句柄 */
static bm_bus_t        g_test_cmd_bus;
/** 测试用 telem_bus 句柄 */
static bm_bus_t        g_test_telem_bus;
/** 模拟控制环消费者（用于读出 set/fault 写入的内容） */
static bm_bus_reader_t g_test_cmd_reader;

/** 测试用 shell 实例 */
BM_SHELL_DEFINE(sc_shell);

/* =========================================================================
 * Unity setUp / tearDown
 * ========================================================================= */

/**
 * @brief 每个测试前：重置 shell 与 bus，注册业务命令
 */
void setUp(void) {
    bm_bus_cfg_t cfg = {.owner_cpu = 0u};

    BM_LOGI("test_shell_cmds", "setUp");

    /* 重置 shell */
    bm_shell_init(&sc_shell);

    /* 重置并配置 cmd_bus（QUEUE，attach 读者→freeze→允许写） */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_bus_open(&g_test_cmd_bus, &sc_cmd_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_bus_reader_attach(&g_test_cmd_bus, &g_test_cmd_reader));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_freeze(&g_test_cmd_bus));

    /* 重置并配置 telem_bus（无读者，仅供 stats 统计打印） */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_bus_open(&g_test_telem_bus, &sc_telem_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_freeze(&g_test_telem_bus));

    /* 注册业务命令 */
    TEST_ASSERT_EQUAL(BM_OK,
        shell_cmds_register(&sc_shell, &g_test_cmd_bus, &g_test_telem_bus));
}

/**
 * @brief 每个测试后：关闭 bus 句柄
 */
void tearDown(void) {
    bm_bus_close(&g_test_cmd_bus);
    bm_bus_close(&g_test_telem_bus);
}

/* =========================================================================
 * 测试：stats 命令
 * ========================================================================= */

/**
 * @brief "stats"（无写入数据）正常返回 BM_OK，不崩溃
 */
void test_cmd_stats_returns_ok(void) {
    char line[] = "stats";

    TEST_ASSERT_EQUAL(BM_OK, bm_shell_exec(&sc_shell, line));
}

/**
 * @brief set 后执行 stats：有写入记录时 stats 仍正常返回 BM_OK
 *
 * 此测试间接验证 stats 在 write_count>0 场景下不崩溃且路径完整。
 */
void test_cmd_stats_after_write_returns_ok(void) {
    char line_set[]   = "set 2.0";
    char line_stats[] = "stats";

    TEST_ASSERT_EQUAL(BM_OK, bm_shell_exec(&sc_shell, line_set));
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_exec(&sc_shell, line_stats));
}

/* =========================================================================
 * 测试：set 命令 → 断言 cmd_bus 写入内容
 * ========================================================================= */

/**
 * @brief "set 3.5" 向 cmd_bus 写入 velocity_setpoint_rad_s=3.5 且 ENABLED 位置位
 *
 * 核心证据：从 cmd_bus QUEUE 读出数据，断言速度值与状态位正确。
 */
void test_cmd_set_writes_velocity_to_bus(void) {
    char line[] = "set 3.5";
    const void *slot;
    bus_servo_command_t cmd;

    /* 执行 shell 命令 → 内部写入 cmd_bus */
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_exec(&sc_shell, line));

    /* 从 cmd_bus 读出写入内容 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&g_test_cmd_reader, &slot));
    memcpy(&cmd, slot, sizeof(cmd));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_release(&g_test_cmd_reader));

    /* 断言：速度设定值 3.5 rad/s */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.5f, cmd.velocity_setpoint_rad_s);
    /* 断言：ENABLED 位置位 */
    TEST_ASSERT_BITS(BUS_SERVO_CMD_STATUS_ENABLED,
                     BUS_SERVO_CMD_STATUS_ENABLED,
                     (int)cmd.status);
}

/**
 * @brief "set"（缺少 value 参数）返回 BM_ERR_INVALID
 */
void test_cmd_set_missing_arg_returns_invalid(void) {
    char line[] = "set";

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_shell_exec(&sc_shell, line));
}

/**
 * @brief "set abc"（非法浮点字符串）返回 BM_ERR_INVALID
 */
void test_cmd_set_invalid_float_returns_invalid(void) {
    char line[] = "set abc";

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_shell_exec(&sc_shell, line));
}

/**
 * @brief "set -1.0" 接受负速度设定值，写入 -1.0 rad/s
 */
void test_cmd_set_negative_velocity_accepted(void) {
    char line[] = "set -1.0";
    const void *slot;
    bus_servo_command_t cmd;

    TEST_ASSERT_EQUAL(BM_OK, bm_shell_exec(&sc_shell, line));

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&g_test_cmd_reader, &slot));
    memcpy(&cmd, slot, sizeof(cmd));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_release(&g_test_cmd_reader));

    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, cmd.velocity_setpoint_rad_s);
}

/* =========================================================================
 * 测试：fault 命令 → 断言 cmd_bus 写入内容
 * ========================================================================= */

/**
 * @brief "fault" 向 cmd_bus 写入 FAULT 位置位、速度清零的停机命令
 *
 * 核心证据：从 cmd_bus QUEUE 读出数据，断言 FAULT 位与速度为 0。
 */
void test_cmd_fault_writes_fault_to_bus(void) {
    char line[] = "fault";
    const void *slot;
    bus_servo_command_t cmd;

    /* 执行 shell 命令 → 内部写入故障命令到 cmd_bus */
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_exec(&sc_shell, line));

    /* 从 cmd_bus 读出写入内容 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&g_test_cmd_reader, &slot));
    memcpy(&cmd, slot, sizeof(cmd));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_release(&g_test_cmd_reader));

    /* 断言：FAULT 位置位 */
    TEST_ASSERT_BITS(BUS_SERVO_CMD_STATUS_FAULT,
                     BUS_SERVO_CMD_STATUS_FAULT,
                     (int)cmd.status);
    /* 断言：速度设定值清零 */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, cmd.velocity_setpoint_rad_s);
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_cmd_stats_returns_ok);
    RUN_TEST(test_cmd_stats_after_write_returns_ok);
    RUN_TEST(test_cmd_set_writes_velocity_to_bus);
    RUN_TEST(test_cmd_set_missing_arg_returns_invalid);
    RUN_TEST(test_cmd_set_invalid_float_returns_invalid);
    RUN_TEST(test_cmd_set_negative_velocity_accepted);
    RUN_TEST(test_cmd_fault_writes_fault_to_bus);
    return UNITY_END();
}
