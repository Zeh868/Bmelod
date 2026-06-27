/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bmelod.h
 * @brief Bmelod Baremetal 统一对外 API 入口
 *
 * 根据 bm_config.h 中 BM_CONFIG_ENABLE_* 暴露已启用子系统。
 * 板级外设请按需 `#include "hal/bm_hal_uart.h"` 或 `bm_hal.h`（不在此聚合）。
 *
 * @par 资源层级与典型配置
 * - Ultra：`BM_CONFIG_ENABLE_ULTRA=1`
 * - Nano/Event：`MODULE`+`WDG`，混合域全关
 * - Lite：在 Nano 基础上按需开 `CHANNEL` / `SHELL`
 * - Control：在 Lite 基础上开 `HRT` / `TICKER` / `EXEC` / `SYNC`
 *
 * CMake 集成：`bmelod_configure(PROFILE ...)` 会自动同步组件开关。
 */
#ifndef BMELOD_H
#define BMELOD_H

#include "bm_config.h"

#if BM_CONFIG_ENABLE_ULTRA

#include "bm_ultra.h"

#else

#include "bm_lite.h"

#if BM_CONFIG_ENABLE_HRT
#include "bm_hybrid.h"
#endif

#endif /* BM_CONFIG_ENABLE_ULTRA */

#endif /* BMELOD_H */
