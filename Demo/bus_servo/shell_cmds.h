/**
 * @file shell_cmds.h
 * @brief bus_servo 示例调试命令注册接口（路线图 #11 shell 业务命令集成）
 *
 * 向 bm_shell 实例注册三条运行期调试命令：
 *   - stats：打印 cmd_bus 与 telem_bus 的 write_count/overflow_count
 *   - set \<value\>：向 cmd_bus 写入 ENABLED + 速度设定值（rad/s）
 *   - fault：向 cmd_bus 注入 FAULT 位、速度清零的停机命令
 *
 * 命令处理函数签名固定（bm_shell_cmd_fn_t：int fn(int, char *[])），
 * 无 context 参数，故通过 shell_cmds_register() 将 bus 句柄注入静态指针。
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
#ifndef SHELL_CMDS_H
#define SHELL_CMDS_H

#include "bm_shell.h"
#include "bm_bus.h"

/**
 * @brief 向 shell 注册 bus_servo 调试命令（stats / set / fault）
 *
 * 初始化内部静态 bus 句柄引用，并向 shell 实例依次注册三条命令：
 *   - stats  ：打印 cmd_bus（必需）与 telem_bus（可选）的 write_count/overflow_count
 *   - set \<v\>：向 cmd_bus 写入 BUS_SERVO_CMD_STATUS_ENABLED + 速度设定值（rad/s）；
 *              参数不足或非法浮点时 bm_shell_puts 提示并返回 BM_ERR_INVALID
 *   - fault  ：向 cmd_bus 写入 BUS_SERVO_CMD_STATUS_FAULT、速度清零的停机命令
 *
 * @note 须在 bm_bus_freeze(cmd_bus) 之后、shell 首次 poll 之前调用。
 *       cmd_bus 的生命期须覆盖 shell 整个运行期。
 *       telem_bus 可为 NULL（stats 仅打印 cmd_bus 统计）。
 *
 * @param shell     目标 shell 实例，非 NULL；须已 bm_shell_init
 * @param cmd_bus   QUEUE 命令总线句柄（set/fault 写入，stats 打印统计）；非 NULL
 * @param telem_bus SIGNAL 遥测总线句柄（stats 打印统计）；可为 NULL
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_NO_MEM 命令表满
 */
int shell_cmds_register(bm_shell_t *shell, bm_bus_t *cmd_bus, bm_bus_t *telem_bus);

#endif /* SHELL_CMDS_H */
