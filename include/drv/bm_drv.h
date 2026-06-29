/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv.h
 * @brief 驱动子系统公共类型与设备定义宏（Zephyr driver API 同构，无 Kconfig）
 *
 * 应用只依赖 include/bm_hal_*.h；厂商 SDK 在 platform/backends/ 中实现
 * struct bm_*_driver_api 并链接对应后端库。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-12
 */
#ifndef BM_DRV_H
#define BM_DRV_H

#include "bm/common/bm_types.h"

/** 设备实例：API 表 + 后端私有配置（均建议 const，占 Flash） */
struct bm_device {
    const void *api;
    const void *config;
};

#define BM_DEVICE_DEFINE(name, device_type, api_ptr, config_ptr) \
    const device_type name = { (api_ptr), (config_ptr) }

#endif /* BM_DRV_H */
