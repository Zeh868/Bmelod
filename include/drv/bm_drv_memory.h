/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_memory.h
 * @brief 内存屏障驱动 API（平台单例后端实现）
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-08-01
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-08-01       1.0            zeh           补全 Doxygen 合规注释
 *
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
