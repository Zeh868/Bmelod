/**
 * @file bm_drv_wdg.h
 * @brief 看门狗驱动 API（平台单例后端实现）
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
