/**
 * @file shell_cmds.c
 * @brief bus_servo 示例：stats/set/fault 调试 shell 命令实现（路线图 #11）
 *
 * 三条命令通过模块静态指针（s_cmd_bus / s_telem_bus）访问 bm_bus 句柄，
 * 由 shell_cmds_register() 注入，不依赖 app_bus_servo_* 辅助函数。
 * 这使单元测试可独立构造 bus 实例并断言写入效果，无需启动完整 demo。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-26       1.0            zeh            初稿：#11 shell 业务命令
 *
 */

#include "shell_cmds.h"
#include "app_bus_servo.h"   /* bus_servo_command_t, BUS_SERVO_CMD_STATUS_* */
#include "bm_log.h"

#include <stdio.h>           /* snprintf */
#include <stdlib.h>          /* strtof   */
#include <string.h>          /* memset, memcpy */

/** 日志标签 */
#define TAG "shell_cmds"

/* =========================================================================
 * 模块静态 bus 句柄（shell_cmds_register 注入；NULL = 未初始化）
 * ========================================================================= */

/** 命令总线句柄（QUEUE；set/fault 写入，stats 统计） */
static bm_bus_t *s_cmd_bus   = NULL;
/** 遥测总线句柄（SIGNAL；stats 统计；可为 NULL） */
static bm_bus_t *s_telem_bus = NULL;

/* =========================================================================
 * 命令处理函数（static，经 bm_shell_register 注册；签名固定 int fn(int,char*[])）
 * ========================================================================= */

/**
 * @brief "stats" 命令：打印 cmd_bus 与 telem_bus 的 write_count/overflow_count
 *
 * 非阻塞读取 bm_bus_stats，通过 bm_shell_puts 输出到 Console CLI。
 * telem_bus 为 NULL 时仅打印 cmd_bus 统计。
 *
 * @param argc 参数个数（忽略额外参数）
 * @param argv argv[0]="stats"
 * @return BM_OK
 */
static int cmd_stats(int argc, char *argv[]) {
    bm_bus_stats_t st;
    char buf[80];

    (void)argc;
    (void)argv;

    if (s_cmd_bus != NULL) {
        if (bm_bus_stats(s_cmd_bus, &st) == BM_OK) {
            (void)snprintf(buf, sizeof(buf),
                           "cmd_bus : write=%u ovf=%u\r\n",
                           (unsigned)st.write_count,
                           (unsigned)st.overflow_count);
            bm_shell_puts(buf);
        }
    }

    if (s_telem_bus != NULL) {
        if (bm_bus_stats(s_telem_bus, &st) == BM_OK) {
            (void)snprintf(buf, sizeof(buf),
                           "telem_bus: write=%u ovf=%u\r\n",
                           (unsigned)st.write_count,
                           (unsigned)st.overflow_count);
            bm_shell_puts(buf);
        }
    }

    return BM_OK;
}

/**
 * @brief "set \<value\>" 命令：向 cmd_bus 写入速度设定值（rad/s）
 *
 * 向 QUEUE 写入 BUS_SERVO_CMD_STATUS_ENABLED + velocity_setpoint_rad_s 命令。
 * argc 不足或 argv[1] 无法转换为浮点时，bm_shell_puts 打印提示并返回 BM_ERR_INVALID。
 *
 * @param argc 参数个数（须 >= 2，argv[1] 为速度字符串）
 * @param argv argv[0]="set"，argv[1]=速度（rad/s）字符串
 * @return BM_OK 成功；BM_ERR_INVALID 参数不足或非法浮点；其他为 bm_bus 错误码
 */
static int cmd_set(int argc, char *argv[]) {
    bus_servo_command_t cmd;
    char *endptr;
    float val;
    void *slot;
    int rc;

    if (argc < 2) {
        bm_shell_puts("usage: set <velocity_rad_s>\r\n");
        return BM_ERR_INVALID;
    }

    val = strtof(argv[1], &endptr);
    if (endptr == argv[1]) {
        /* strtof 未消耗任何字符：输入不是有效浮点数 */
        bm_shell_puts("set: invalid float value\r\n");
        return BM_ERR_INVALID;
    }

    if (s_cmd_bus == NULL) {
        bm_shell_puts("set: cmd_bus not initialized\r\n");
        return BM_ERR_INVALID;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.status                  = BUS_SERVO_CMD_STATUS_ENABLED;
    cmd.velocity_setpoint_rad_s = val;

    rc = bm_bus_acquire_write(s_cmd_bus, &slot);
    if (rc != BM_OK) {
        BM_LOGW(TAG, "set: acquire_write rc=%d", rc);
        return rc;
    }
    memcpy(slot, &cmd, sizeof(cmd));
    return bm_bus_commit(s_cmd_bus);
}

/**
 * @brief "fault" 命令：向 cmd_bus 注入故障停机命令
 *
 * 写入 BUS_SERVO_CMD_STATUS_FAULT 位、速度清零的命令，
 * 触发控制环 bus_servo_control_step 的安全停机分支。
 *
 * @param argc 参数个数（忽略额外参数）
 * @param argv argv[0]="fault"
 * @return BM_OK 成功；BM_ERR_INVALID cmd_bus 未初始化；其他为 bm_bus 错误码
 */
static int cmd_fault(int argc, char *argv[]) {
    bus_servo_command_t cmd;
    void *slot;
    int rc;

    (void)argc;
    (void)argv;

    if (s_cmd_bus == NULL) {
        bm_shell_puts("fault: cmd_bus not initialized\r\n");
        return BM_ERR_INVALID;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.status                  = BUS_SERVO_CMD_STATUS_FAULT;
    cmd.velocity_setpoint_rad_s = 0.0f;

    rc = bm_bus_acquire_write(s_cmd_bus, &slot);
    if (rc != BM_OK) {
        BM_LOGW(TAG, "fault: acquire_write rc=%d", rc);
        return rc;
    }
    memcpy(slot, &cmd, sizeof(cmd));
    return bm_bus_commit(s_cmd_bus);
}

/* =========================================================================
 * 公共 API
 * ========================================================================= */

/**
 * @brief 初始化静态 bus 引用并向 shell 注册 stats/set/fault 三条命令
 *
 * 依次调用 bm_shell_register 注册三条命令；任一注册失败时提前返回错误码。
 * 可安全地在 bm_shell_init 之后多次调用（setUp 场景下每次 bm_shell_init
 * 会清空命令表，重新注册不会产生重复条目）。
 *
 * @param shell     目标 shell 实例，非 NULL
 * @param cmd_bus   QUEUE 命令总线句柄；非 NULL
 * @param telem_bus SIGNAL 遥测总线句柄；可为 NULL
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_NO_MEM 命令表满
 */
int shell_cmds_register(bm_shell_t *shell, bm_bus_t *cmd_bus, bm_bus_t *telem_bus) {
    int rc;

    if (shell == NULL || cmd_bus == NULL) {
        return BM_ERR_INVALID;
    }

    s_cmd_bus   = cmd_bus;
    s_telem_bus = telem_bus; /* NULL 合法：stats 仅打印 cmd_bus */

    rc = bm_shell_register(shell, "stats", cmd_stats,
                           "打印 bus 统计（write_count/overflow_count）");
    if (rc != BM_OK) {
        return rc;
    }

    rc = bm_shell_register(shell, "set", cmd_set,
                           "set <val>: 更新速度设定值（rad/s）");
    if (rc != BM_OK) {
        return rc;
    }

    return bm_shell_register(shell, "fault", cmd_fault,
                             "注入故障停机命令");
}
