/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_wdg.h
 * @brief 看门狗驱动 API（平台单例后端实现）
 * @maturity E1
 * @author Bmelod contributors
 * @version 1.0
 * @date 2026-08-01
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-08-01       1.0            Codex           补全 Doxygen 合规注释
 *
 */
#ifndef BM_DRV_WDG_H
#define BM_DRV_WDG_H

#include <stdint.h>

struct bm_wdg_driver_api {
    int (*init)(uint32_t timeout_ms);
    void (*feed)(void);
};

#ifdef BM_DRV_WDG_API
extern const struct bm_wdg_driver_api bm_drv_wdg_api;
#endif

#endif /* BM_DRV_WDG_H */
