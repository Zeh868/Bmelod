/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_hrtimer.c
 * @brief 高精度 Timer HAL 分发层
 *
 * 未绑定后端时返回 BM_ERR_NOT_INIT（对齐既有分发层模式）。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增高精度 Timer HAL 分发
 */
#include "hal/bm_hal_hrtimer.h"
#include "bm/common/bm_types.h"

#include <stddef.h>

int bm_hal_hrtimer_init(const bm_hal_hrtimer_t *dev, void *config) {
    if (dev == NULL || dev->api == NULL || dev->api->init == NULL) {
        return BM_ERR_NOT_INIT;
    }
    return dev->api->init(dev, config != NULL ? config : (void *)dev->config);
}

int bm_hal_hrtimer_start(const bm_hal_hrtimer_t *dev,
                         uint32_t mode, uint32_t period_us) {
    if (dev == NULL || dev->api == NULL || dev->api->start == NULL) {
        return BM_ERR_NOT_INIT;
    }
    if (mode != BM_HRTIMER_MODE_PERIODIC && mode != BM_HRTIMER_MODE_ONESHOT) {
        return BM_ERR_INVALID;
    }
    return dev->api->start(dev, mode, period_us);
}

int bm_hal_hrtimer_stop(const bm_hal_hrtimer_t *dev) {
    if (dev == NULL || dev->api == NULL || dev->api->stop == NULL) {
        return BM_ERR_NOT_INIT;
    }
    return dev->api->stop(dev);
}

int bm_hal_hrtimer_set_compare(const bm_hal_hrtimer_t *dev, uint32_t compare_us) {
    if (dev == NULL || dev->api == NULL || dev->api->set_compare == NULL) {
        return BM_ERR_NOT_INIT;
    }
    return dev->api->set_compare(dev, compare_us);
}

uint32_t bm_hal_hrtimer_get_freq(const bm_hal_hrtimer_t *dev) {
    if (dev == NULL || dev->api == NULL || dev->api->get_freq == NULL) {
        return 0u;
    }
    return dev->api->get_freq(dev);
}

uint32_t bm_hal_hrtimer_get_resolution_ns(const bm_hal_hrtimer_t *dev) {
    if (dev == NULL || dev->api == NULL || dev->api->get_resolution_ns == NULL) {
        return 0u;
    }
    return dev->api->get_resolution_ns(dev);
}

uint32_t bm_hal_hrtimer_get_max_period_us(const bm_hal_hrtimer_t *dev) {
    if (dev == NULL || dev->api == NULL || dev->api->get_max_period_us == NULL) {
        return 0u;
    }
    return dev->api->get_max_period_us(dev);
}

uint32_t bm_hal_hrtimer_get_min_period_us(const bm_hal_hrtimer_t *dev) {
    if (dev == NULL || dev->api == NULL || dev->api->get_min_period_us == NULL) {
        return 0u;
    }
    return dev->api->get_min_period_us(dev);
}

int bm_hal_hrtimer_get_stats(const bm_hal_hrtimer_t *dev,
                             bm_hrtimer_stats_t *stats) {
    if (dev == NULL || dev->api == NULL || dev->api->get_stats == NULL) {
        return BM_ERR_NOT_INIT;
    }
    if (stats == NULL) {
        return BM_ERR_INVALID;
    }
    return dev->api->get_stats(dev, stats);
}

int bm_hal_hrtimer_set_callback(const bm_hal_hrtimer_t *dev,
                                bm_hrtimer_callback_t cb, void *user) {
    if (dev == NULL || dev->api == NULL || dev->api->set_callback == NULL) {
        return BM_ERR_NOT_INIT;
    }
    return dev->api->set_callback(dev, cb, user);
}
