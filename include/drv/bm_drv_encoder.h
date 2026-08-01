/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_encoder.h
 * @brief 编码器设备驱动 API
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
#ifndef BM_DRV_ENCODER_H
#define BM_DRV_ENCODER_H

#include "drv/bm_drv.h"
#include "bm/common/bm_types.h"

#include <stdint.h>

struct bm_hal_encoder;

struct bm_encoder_driver_api {
    int (*read)(const struct bm_hal_encoder *dev, int32_t *value);
};

struct bm_hal_encoder {
    const struct bm_encoder_driver_api *api;
    const void                         *config;
};

#endif /* BM_DRV_ENCODER_H */
