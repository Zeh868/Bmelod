/**
 * @file bm_drv_comp.h
 * @brief 比较器设备驱动 API
 */
#ifndef BM_DRV_COMP_H
#define BM_DRV_COMP_H

struct bm_hal_comp;

#include "drv/bm_drv.h"
#include "bm/common/bm_types.h"

struct bm_comp_driver_api {
    int (*clear_latch)(const struct bm_hal_comp *dev);
};

struct bm_hal_comp {
    const struct bm_comp_driver_api *api;
    const void                      *config;
};

#endif /* BM_DRV_COMP_H */
