/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_can.c
 * @brief CAN/FDCAN HAL 分发层（统一实例模型：契约 → driver API）
 *
 * 设备未绑定后端（api 为 NULL）时返回 BM_ERR_NOT_INIT / 0。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 CAN/FDCAN HAL 分发层
 */
#include "bm_hal_can.h"
#include "bm_config.h"
#include "bm_types.h"

int bm_hal_can_init(const struct bm_hal_can *dev, void *config) {
    if (!dev || !dev->api || !dev->api->init) {
        return BM_ERR_NOT_INIT;
    }
    return dev->api->init(dev, config);
}

int bm_hal_can_start(const struct bm_hal_can *dev) {
    if (!dev || !dev->api || !dev->api->start) {
        return BM_ERR_NOT_INIT;
    }
    return dev->api->start(dev);
}

int bm_hal_can_stop(const struct bm_hal_can *dev) {
    if (!dev || !dev->api || !dev->api->stop) {
        return BM_ERR_NOT_INIT;
    }
    return dev->api->stop(dev);
}

int bm_hal_can_send(const struct bm_hal_can *dev, const bm_can_frame_t *frame) {
    if (!dev || !dev->api || !dev->api->send) {
        return BM_ERR_NOT_INIT;
    }
    if (!frame) {
        return BM_ERR_INVALID;
    }
    return dev->api->send(dev, frame);
}

int bm_hal_can_add_filter(const struct bm_hal_can *dev,
                          const bm_can_filter_t *filter,
                          uint32_t *filter_id) {
    if (!dev || !dev->api || !dev->api->add_filter) {
        return BM_ERR_NOT_INIT;
    }
    if (!filter) {
        return BM_ERR_INVALID;
    }
    return dev->api->add_filter(dev, filter, filter_id);
}

int bm_hal_can_remove_filter(const struct bm_hal_can *dev, uint32_t filter_id) {
    if (!dev || !dev->api || !dev->api->remove_filter) {
        return BM_ERR_NOT_INIT;
    }
    return dev->api->remove_filter(dev, filter_id);
}

uint32_t bm_hal_can_get_capabilities(const struct bm_hal_can *dev) {
    if (!dev || !dev->api || !dev->api->get_capabilities) {
        return 0u;
    }
    return dev->api->get_capabilities(dev);
}

int bm_hal_can_get_stats(const struct bm_hal_can *dev, bm_can_stats_t *stats) {
    if (!dev || !dev->api || !dev->api->get_stats) {
        return BM_ERR_NOT_INIT;
    }
    if (!stats) {
        return BM_ERR_INVALID;
    }
    return dev->api->get_stats(dev, stats);
}

int bm_hal_can_set_rx_callback(const struct bm_hal_can *dev,
                               bm_can_rx_callback_t cb, void *user) {
    if (!dev || !dev->api || !dev->api->set_rx_callback) {
        return BM_ERR_NOT_INIT;
    }
    return dev->api->set_rx_callback(dev, cb, user);
}

int bm_hal_can_set_event_callback(const struct bm_hal_can *dev,
                                  bm_can_event_callback_t cb, void *user) {
    if (!dev || !dev->api || !dev->api->set_event_callback) {
        return BM_ERR_NOT_INIT;
    }
    return dev->api->set_event_callback(dev, cb, user);
}
