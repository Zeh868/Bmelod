/**
 * @file bm_mp_port_impl.c
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @brief 多核看门狗放行钩子安装（取代旧 bm_mp_port 弱符号 seam）
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-16
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-16       1.0            zeh            仓库拆分：WDG 端口实现
 *
 */
#include "bm/core/bm_wdg.h"
#include "bm/mp/bm_mp_wdg.h"

static int mp_wdg_gate(void) {
    return bm_mp_wdg_bootstrap_check();
}

void bm_mp_install_wdg_gate(void) {
    bm_wdg_set_gate_hook(mp_wdg_gate);
}
