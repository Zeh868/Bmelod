/**
 * @file bm_drv_memory.h
 * @brief 内存屏障驱动 API（平台单例后端实现）
 */
#ifndef BM_DRV_MEMORY_H
#define BM_DRV_MEMORY_H

struct bm_memory_driver_api {
    void (*barrier_release)(void);
    void (*barrier_full)(void);
};

#ifdef BM_DRV_MEMORY_API
extern const struct bm_memory_driver_api bm_drv_memory_api;
#endif

#endif /* BM_DRV_MEMORY_H */
