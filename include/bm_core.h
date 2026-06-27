/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_core.h
 * @brief 核心子系统聚合头（atomic、event、mempool、types）
 *
 * 应用推荐入口：`#include "bmelod.h"`（按 bm_config.h 裁剪）。
 * 仅需事件子系统时可单独包含本头；Lite 层见 bm_lite.h。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 *
 */
#ifndef BM_CORE_H
#define BM_CORE_H

#include "bm/common/bm_atomic.h"
#include "bm/core/bm_event.h"
#include "bm/core/bm_mempool.h"
#include "bm/common/bm_types.h"

#endif /* BM_CORE_H */
