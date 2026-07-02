/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hybrid.h
 * @brief Control 层聚合头：HRT / Ticker / 多实例控制 / 同步域
 *
 * 要求 BM_CONFIG_ENABLE_HRT=1。子组件由 BM_CONFIG_ENABLE_* 进一步裁剪。
 */
#ifndef BM_HYBRID_H
#define BM_HYBRID_H

#include "bm_config.h"

#if !BM_CONFIG_ENABLE_HRT
#error "bm_hybrid.h requires BM_CONFIG_ENABLE_HRT"
#endif

#if BM_CONFIG_ENABLE_SYNC && !BM_CONFIG_ENABLE_EXEC
#error "BM_CONFIG_ENABLE_SYNC requires BM_CONFIG_ENABLE_EXEC"
#endif

#if BM_CONFIG_ENABLE_EXEC && !BM_CONFIG_ENABLE_HRT
#error "BM_CONFIG_ENABLE_EXEC requires BM_CONFIG_ENABLE_HRT"
#endif

#include "bm/hybrid/bm_hrt.h"

#if BM_CONFIG_ENABLE_TICKER
#include "bm/hybrid/bm_ticker.h"
#endif

#if BM_CONFIG_ENABLE_EXEC
#include "bm/hybrid/bm_resource.h"
#include "bm/hybrid/bm_exec.h"
#include "bm/hybrid/bm_runtime_model.h"
#endif

#if BM_CONFIG_ENABLE_SYNC
#include "bm/hybrid/bm_sync.h"
#endif

#if BM_CONFIG_ENABLE_STREAM
#include "bm/hybrid/bm_timestamp.h"
#include "bm/hybrid/bm_block.h"
#include "bm/hybrid/bm_stream.h"
#endif

#if BM_CONFIG_ENABLE_PIPELINE
#include "bm/hybrid/bm_pipeline.h"
#endif

#if BM_CONFIG_ENABLE_TT_SCHED
#include "bm/hybrid/bm_tt_schedule.h"
#endif

#if BM_CONFIG_ENABLE_WCET_MON
#include "bm/hybrid/bm_wcet_mon.h"
#endif

#endif /* BM_HYBRID_H */
