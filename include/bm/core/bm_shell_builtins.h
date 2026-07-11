/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_shell_builtins.h
 * @brief Shell 内建命令组：param/log/ver/uptime（批 P）
 *
 * 提供一把注册接口，向指定 bm_shell 实例注册四条框架内建调试/整定
 * 命令：`param`（bm_param 前端，运行期参数表整定操作台）、`log`
 * （运行期日志级别阈值）、`ver`（固件编译时间）、`uptime`（开机运行
 * 时长）。命令面是否启用、何时注册，控制权仍在 app（本函数只在 app
 * 主动调用时生效，不自动挂载）。
 *
 * @note 依赖 BM_ENABLE_PARAM（`param` 子命令经 bm_param API 落地）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-11
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-11       1.0            zeh            正式发布（批 P：shell 内建命令组）
 *
 */
#ifndef BM_SHELL_BUILTINS_H
#define BM_SHELL_BUILTINS_H

#include "bm/core/bm_shell.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 向 shell 实例注册全部内建命令（param/log/ver/uptime）
 *
 * 任一命令注册失败（如命令表已满）立即返回该错误码，不再继续注册
 * 后续命令。
 *
 * @param shell Shell 实例指针
 * @return BM_OK 全部注册成功；BM_ERR_INVALID shell 为 NULL；
 *         其余为 bm_shell_register 透传的失败错误码
 */
int bm_shell_register_builtins(bm_shell_t *shell);

#ifdef __cplusplus
}
#endif

#endif /* BM_SHELL_BUILTINS_H */
