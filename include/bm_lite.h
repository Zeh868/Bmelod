/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_lite.h
 * @brief Lite 层聚合头：核心事件子系统 + 可选应用组件
 *
 * 根据 bm_config.h 中 BM_CONFIG_ENABLE_* 暴露 module / shell / wdg。
 * 混合域请另含 bm_hybrid.h 或使用 bmelod.h。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-08-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-08-01       1.0            zeh           补齐聚合头 Doxygen 元数据
 */
#ifndef BM_LITE_H
#define BM_LITE_H

#include "bm_config.h"

#include "bm_core.h"
#include "bm/common/bm_log.h"

#if BM_CONFIG_ENABLE_MODULE
#include "bm/core/bm_module.h"
#endif

#if BM_CONFIG_ENABLE_SHELL
#include "bm/core/bm_shell.h"
#endif

#if BM_CONFIG_ENABLE_WDG
#include "bm/core/bm_wdg.h"
#endif

#endif /* BM_LITE_H */
