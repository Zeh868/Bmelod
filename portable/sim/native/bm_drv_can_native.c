/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_can_native.c
 * @brief native_sim 多实例 CAN/FDCAN 后端
 * @maturity E1
 *
 * 支持 2 个实例：
 *   - bm_can_default（实例 0）：console 语义，send 打印到 stdout。
 *   - bm_native_can1（实例 1）：loopback 语义，send 数据进入 TX 测试缓冲区，
 *     测试可通过 bm_hal_can_native_inject_rx 注入 RX 帧，通过 inject_event 注入事件。
 *
 * 全部静态分配，零动态内存。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 native_sim 多实例 CAN 后端
 * 2026-07-28       1.1            zeh            新增 RX 缓冲读取接口
 *                                             bm_hal_can_native_rx_frame；
 *                                             裸 -1 哨兵改 BM_ERR_*；
 *                                             注明 TX_COMPLETE 同步派发契约
 * 2026-08-01       1.1            zeh            补全中文 Doxygen 合规注释
 */
#include "bm_hal_can_native.h"
#include "bm/common/bm_types.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/** @brief CAN 实例数。 */
#define BM_NATIVE_CAN_COUNT 2u

/** @brief 最大过滤器数。 */
#define BM_NATIVE_CAN_FILTER_MAX 8u

/** @brief 最大 TX 测试帧缓存数（实例 1）。 */
#define BM_NATIVE_CAN_TX_BUF_MAX 8u

/** @brief 最大 RX 注入缓存数（未注册回调时）。 */
#define BM_NATIVE_CAN_RX_BUF_MAX 8u

/** @brief native CAN 实例索引。 */
typedef enum {
    BM_NATIVE_CAN_INDEX_DEFAULT = 0u,
    BM_NATIVE_CAN_INDEX_NATIVE1 = 1u,
} bm_native_can_index_t;

/** @brief 过滤器槽。 */
typedef struct {
    int            used;
    bm_can_filter_t cfg;
} bm_native_can_filter_t;

/** @brief 单个 CAN 实例状态。 */
typedef struct {
    int                          initialized;
    int                          started;
    bm_can_rx_callback_t         rx_cb;
    void                        *rx_user;
    bm_can_event_callback_t      event_cb;
    void                        *event_user;
    bm_can_stats_t               stats;
    bm_native_can_filter_t       filters[BM_NATIVE_CAN_FILTER_MAX];
    bm_can_frame_t               tx_buf[BM_NATIVE_CAN_TX_BUF_MAX];
    size_t                       tx_count;
    bm_can_frame_t               rx_buf[BM_NATIVE_CAN_RX_BUF_MAX];
    size_t                       rx_count;
} bm_native_can_state_t;

static bm_native_can_state_t s_states[BM_NATIVE_CAN_COUNT];

/**
 * @brief 由设备实例获取索引；无效时返回负值错误码（BM_ERR_INVALID）。
 */
static int bm_native_can_index_for(const struct bm_hal_can *dev) {
    if (dev == &bm_can_default) {
        return (int)BM_NATIVE_CAN_INDEX_DEFAULT;
    }
    if (dev == &bm_native_can1) {
        return (int)BM_NATIVE_CAN_INDEX_NATIVE1;
    }
    return BM_ERR_INVALID;
}

/* -------------------------------------------------------------------------- */
/*  测试辅助接口                                                                */
/* -------------------------------------------------------------------------- */

void bm_hal_can_native_reset(void) {
    uint32_t i;

    for (i = 0u; i < BM_NATIVE_CAN_COUNT; ++i) {
        (void)memset(&s_states[i], 0, sizeof(s_states[i]));
    }
}

void bm_hal_can_native_inject_rx(const struct bm_hal_can *dev,
                                 const bm_can_frame_t *frame) {
    int idx;
    bm_native_can_state_t *state;

    idx = bm_native_can_index_for(dev);
    if (idx < 0 || frame == NULL) {
        return;
    }
    state = &s_states[idx];
    state->stats.rx_count++;

    if (state->rx_cb != NULL) {
        state->rx_cb(dev, frame, state->rx_user);
    } else if (state->rx_count < BM_NATIVE_CAN_RX_BUF_MAX) {
        state->rx_buf[state->rx_count++] = *frame;
    } else {
        state->stats.rx_overflow_count++;
        state->stats.last_errors |= BM_CAN_EVT_RX_OVERFLOW;
    }
}

void bm_hal_can_native_inject_event(const struct bm_hal_can *dev,
                                    uint32_t event) {
    int idx;
    bm_native_can_state_t *state;

    idx = bm_native_can_index_for(dev);
    if (idx < 0) {
        return;
    }
    state = &s_states[idx];
    state->stats.last_errors |= event;

    if ((event & BM_CAN_EVT_BUS_OFF) != 0u) {
        state->stats.bus_off_count++;
    }
    if ((event & BM_CAN_EVT_ERROR_WARNING) != 0u) {
        state->stats.error_warning_count++;
    }
    if ((event & BM_CAN_EVT_ERROR_PASSIVE) != 0u) {
        state->stats.error_passive_count++;
    }
    if ((event & BM_CAN_EVT_RX_OVERFLOW) != 0u) {
        state->stats.rx_overflow_count++;
    }
    if ((event & BM_CAN_EVT_TX_TIMEOUT) != 0u) {
        state->stats.tx_timeout_count++;
    }

    if (state->event_cb != NULL) {
        state->event_cb(dev, event, state->event_user);
    }
}

void bm_hal_can_native_trigger_bus_off(const struct bm_hal_can *dev) {
    bm_hal_can_native_inject_event(dev, BM_CAN_EVT_BUS_OFF);
}

void bm_hal_can_native_recover_bus_off(const struct bm_hal_can *dev) {
    bm_hal_can_native_inject_event(dev, BM_CAN_EVT_BUS_OFF_RECOVER);
}

size_t bm_hal_can_native_tx_count(const struct bm_hal_can *dev) {
    int idx;

    idx = bm_native_can_index_for(dev);
    if (idx < 0) {
        return 0u;
    }
    return s_states[idx].tx_count;
}

int bm_hal_can_native_tx_frame(const struct bm_hal_can *dev,
                               bm_can_frame_t *frame) {
    int idx;
    bm_native_can_state_t *state;

    idx = bm_native_can_index_for(dev);
    if (idx < 0 || frame == NULL) {
        return BM_ERR_INVALID;
    }
    state = &s_states[idx];
    if (state->tx_count == 0u) {
        return BM_ERR_INVALID;
    }
    *frame = state->tx_buf[state->tx_count - 1u];
    return BM_OK;
}

int bm_hal_can_native_rx_frame(const struct bm_hal_can *dev,
                               bm_can_frame_t *frame) {
    int idx;
    bm_native_can_state_t *state;

    idx = bm_native_can_index_for(dev);
    if (idx < 0 || frame == NULL) {
        return BM_ERR_INVALID;
    }
    state = &s_states[idx];
    if (state->rx_count == 0u) {
        return BM_ERR_INVALID;
    }
    *frame = state->rx_buf[0];
    state->rx_count--;
    if (state->rx_count > 0u) {
        (void)memmove(&state->rx_buf[0], &state->rx_buf[1],
                      state->rx_count * sizeof(state->rx_buf[0]));
    }
    return BM_OK;
}

size_t bm_hal_can_native_filter_count(const struct bm_hal_can *dev) {
    int idx;
    size_t i;
    size_t n = 0u;

    idx = bm_native_can_index_for(dev);
    if (idx < 0) {
        return 0u;
    }
    for (i = 0u; i < BM_NATIVE_CAN_FILTER_MAX; ++i) {
        if (s_states[idx].filters[i].used) {
            n++;
        }
    }
    return n;
}

/* -------------------------------------------------------------------------- */
/*  driver API 实现                                                             */
/* -------------------------------------------------------------------------- */

static int native_can_validate_frame(const bm_can_frame_t *frame) {
    if (frame->dlc > BM_CAN_MAX_DLC) {
        return BM_ERR_INVALID;
    }
    if ((frame->flags & BM_CAN_FLAG_EXT) != 0u) {
        if (frame->id > BM_CAN_EXT_ID_MAX) {
            return BM_ERR_INVALID;
        }
    } else {
        if (frame->id > BM_CAN_STD_ID_MAX) {
            return BM_ERR_INVALID;
        }
    }
    if ((frame->flags & BM_CAN_FLAG_FD) == 0u && frame->dlc > 8u) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

/**
 * @brief 初始化CAN设备。
 * @param dev CAN 设备实例。
 * @param config 设备初始化配置；当前实现不使用该参数。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int native_can_init(const struct bm_hal_can *dev, void *config) {
    int idx;
    bm_native_can_state_t *state;

    (void)config;
    idx = bm_native_can_index_for(dev);
    if (idx < 0) {
        return BM_ERR_INVALID;
    }
    state = &s_states[idx];
    state->initialized = 1;
    state->started = 0;
    return BM_OK;
}

/**
 * @brief 启动CAN设备。
 * @param dev CAN 设备实例。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID；设备未初始化时返回 BM_ERR_NOT_INIT。
 */
static int native_can_start(const struct bm_hal_can *dev) {
    int idx;
    bm_native_can_state_t *state;

    idx = bm_native_can_index_for(dev);
    if (idx < 0) {
        return BM_ERR_INVALID;
    }
    state = &s_states[idx];
    if (!state->initialized) {
        return BM_ERR_NOT_INIT;
    }
    state->started = 1;
    return BM_OK;
}

/**
 * @brief 停止CAN设备。
 * @param dev CAN 设备实例。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int native_can_stop(const struct bm_hal_can *dev) {
    int idx;
    bm_native_can_state_t *state;

    idx = bm_native_can_index_for(dev);
    if (idx < 0) {
        return BM_ERR_INVALID;
    }
    state = &s_states[idx];
    state->started = 0;
    return BM_OK;
}

/**
 * @brief 通过CAN发送数据。
 * @param dev CAN 设备实例。
 * @param frame 待发送的 CAN 帧；不得为 NULL。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID；设备未初始化时返回 BM_ERR_NOT_INIT；资源忙或队列已满时返回 BM_ERR_BUSY；底层操作失败时透传其错误码。
 */
static int native_can_send(const struct bm_hal_can *dev,
                           const bm_can_frame_t *frame) {
    int idx;
    bm_native_can_state_t *state;
    int rc;

    idx = bm_native_can_index_for(dev);
    if (idx < 0) {
        return BM_ERR_INVALID;
    }
    state = &s_states[idx];
    if (!state->initialized) {
        return BM_ERR_NOT_INIT;
    }
    if (frame == NULL) {
        return BM_ERR_INVALID;
    }
    rc = native_can_validate_frame(frame);
    if (rc != BM_OK) {
        return rc;
    }

    if (dev == &bm_can_default) {
        /* 实例 0：console 输出帧摘要 */
        (void)printf("CAN tx id=0x%08lX dlc=%u\n",
                     (unsigned long)frame->id, (unsigned)frame->dlc);
    } else {
        /* 实例 1：记录到测试缓冲区 */
        if (state->tx_count >= BM_NATIVE_CAN_TX_BUF_MAX) {
            return BM_ERR_BUSY;
        }
        state->tx_buf[state->tx_count++] = *frame;
    }

    state->stats.tx_count++;
    /* TX_COMPLETE 同步派发（send 返回前）；按 drv 契约回调内禁止重入 send */
    if (state->event_cb != NULL) {
        state->event_cb(dev, BM_CAN_EVT_TX_COMPLETE, state->event_user);
    }
    return BM_OK;
}

/**
 * @brief 查找第一个未使用的 CAN 过滤器槽位。
 * @param state 进入临界区前保存的中断状态。
 * @return 成功返回槽位索引；无可用槽位时返回 BM_ERR_NO_MEM。
 */
static int native_can_find_free_filter(bm_native_can_state_t *state) {
    size_t i;

    for (i = 0u; i < BM_NATIVE_CAN_FILTER_MAX; ++i) {
        if (!state->filters[i].used) {
            return (int)i;
        }
    }
    return BM_ERR_NO_MEM; /* 无空闲槽（负值） */
}

/**
 * @brief 向CAN设备添加接收过滤器。
 * @param dev CAN 设备实例。
 * @param filter CAN 过滤器配置；不得为 NULL。
 * @param filter_id 用于接收新过滤器标识符的输出指针；不得为 NULL。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID；无可用静态槽位时返回 BM_ERR_NO_MEM。
 */
static int native_can_add_filter(const struct bm_hal_can *dev,
                                 const bm_can_filter_t *filter,
                                 uint32_t *filter_id) {
    int idx;
    bm_native_can_state_t *state;
    int slot;

    idx = bm_native_can_index_for(dev);
    if (idx < 0) {
        return BM_ERR_INVALID;
    }
    state = &s_states[idx];

    if (filter == NULL) {
        return BM_ERR_INVALID;
    }
    if (filter->type > BM_CAN_FILTER_TYPE_LIST ||
        filter->fifo > BM_CAN_FILTER_FIFO1) {
        return BM_ERR_INVALID;
    }

    slot = native_can_find_free_filter(state);
    if (slot < 0) {
        return BM_ERR_NO_MEM;
    }
    state->filters[slot].used = 1;
    state->filters[slot].cfg = *filter;
    if (filter_id != NULL) {
        *filter_id = (uint32_t)slot;
    }
    return BM_OK;
}

/**
 * @brief 从CAN设备移除接收过滤器。
 * @param dev CAN 设备实例。
 * @param filter_id CAN 过滤器标识符。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int native_can_remove_filter(const struct bm_hal_can *dev,
                                    uint32_t filter_id) {
    int idx;
    bm_native_can_state_t *state;

    idx = bm_native_can_index_for(dev);
    if (idx < 0) {
        return BM_ERR_INVALID;
    }
    state = &s_states[idx];
    if (filter_id >= BM_NATIVE_CAN_FILTER_MAX || !state->filters[filter_id].used) {
        return BM_ERR_INVALID;
    }
    state->filters[filter_id].used = 0;
    (void)memset(&state->filters[filter_id].cfg, 0,
                 sizeof(state->filters[filter_id].cfg));
    return BM_OK;
}

/**
 * @brief 读取CAN设备能力位掩码。
 * @param dev CAN 设备实例；当前实现不使用该参数。
 * @return 设备能力位掩码；设备无效时返回 0。
 */
static uint32_t native_can_get_capabilities(const struct bm_hal_can *dev) {
    (void)dev;
    return BM_CAN_CAP_FD | BM_CAN_CAP_STD_FILTER | BM_CAN_CAP_EXT_FILTER |
           BM_CAN_CAP_FIFO0 | BM_CAN_CAP_FIFO1 | BM_CAN_CAP_TX_FIFO;
}

/**
 * @brief 读取CAN运行统计。
 * @param dev CAN 设备实例。
 * @param stats 用于接收运行统计的输出结构；不得为 NULL。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int native_can_get_stats(const struct bm_hal_can *dev,
                                bm_can_stats_t *stats) {
    int idx;
    bm_native_can_state_t *state;

    idx = bm_native_can_index_for(dev);
    if (idx < 0 || stats == NULL) {
        return BM_ERR_INVALID;
    }
    state = &s_states[idx];
    *stats = state->stats;
    state->stats.last_errors = 0u; /* 读取后清零最近一次错误 */
    return BM_OK;
}

/**
 * @brief 设置CAN接收回调。
 * @param dev CAN 设备实例。
 * @param cb 接收回调；传入 NULL 时解除绑定。
 * @param user 回调用户上下文，调用回调时原样传入。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int native_can_set_rx_callback(const struct bm_hal_can *dev,
                                      bm_can_rx_callback_t cb, void *user) {
    int idx;
    bm_native_can_state_t *state;

    idx = bm_native_can_index_for(dev);
    if (idx < 0) {
        return BM_ERR_INVALID;
    }
    state = &s_states[idx];
    state->rx_cb = cb;
    state->rx_user = user;
    return BM_OK;
}

/**
 * @brief 设置CAN事件回调。
 * @param dev CAN 设备实例。
 * @param cb 事件回调；传入 NULL 时解除绑定。
 * @param user 回调用户上下文，调用回调时原样传入。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int native_can_set_event_callback(const struct bm_hal_can *dev,
                                         bm_can_event_callback_t cb,
                                         void *user) {
    int idx;
    bm_native_can_state_t *state;

    idx = bm_native_can_index_for(dev);
    if (idx < 0) {
        return BM_ERR_INVALID;
    }
    state = &s_states[idx];
    state->event_cb = cb;
    state->event_user = user;
    return BM_OK;
}

static const struct bm_can_driver_api g_native_can_api = {
    native_can_init,
    native_can_start,
    native_can_stop,
    native_can_send,
    native_can_add_filter,
    native_can_remove_filter,
    native_can_get_capabilities,
    native_can_get_stats,
    native_can_set_rx_callback,
    native_can_set_event_callback,
};

const struct bm_hal_can bm_can_default = { &g_native_can_api, NULL };
const struct bm_hal_can bm_native_can1 = { &g_native_can_api, NULL };
