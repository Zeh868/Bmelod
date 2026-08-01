/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_uart_native.c
 * @brief native_sim 多实例 UART 后端
 * @maturity E1
 *
 * 支持 2 个实例：
 *   - bm_uart_default（实例 0）：console 语义，send 写到 stdout。
 *   - bm_native_uart1（实例 1）：测试记录语义，send 数据追加到 TX 测试缓冲区
 *     （不自动 loopback），经 bm_hal_uart_native_tx_count/tx_byte 读取；
 *     RX 由测试通过 bm_hal_uart_native_put_rx 注入。
 *
 * 支持 ring buffer RX、IDLE 事件、TX 完成回调、错误统计。
 * 全部静态分配，零动态内存。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 native_sim 多实例 UART 后端
 * 2026-07-28       1.1            zeh            reset 全量复位；TX 测试缓冲满返回
 *                                             BM_ERR_BUSY；删除未用 ring_free；
 *                                             ring 裸 -1 改 BM_ERR_*；注释对齐实现
 * 2026-08-01       1.1            zeh            补全中文 Doxygen 合规注释
 */
#include "bm_drv_uart.h"
#include "hal/bm_hal_uart.h"
#include "bm_hal_uart_native.h"
#include "bm/common/bm_types.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/** @brief UART 实例数。 */
#define BM_NATIVE_UART_COUNT 2u

/** @brief 实例 1 TX 测试缓冲区大小。 */
#define BM_NATIVE_UART_TX_BUF_SIZE 256u

/** @brief 环形缓冲区状态。 */
typedef struct {
    uint8_t *buf;
    size_t   len;
    size_t   head;
    size_t   tail;
} bm_native_uart_ring_t;

/** @brief 单个 UART 实例状态。 */
typedef struct {
    bm_native_uart_ring_t       rx_ring;
    void                      (*rx_byte_cb)(uint8_t c);
    bm_uart_tx_complete_callback_t tx_complete_cb;
    void                       *tx_complete_user;
    bm_uart_rx_frame_callback_t rx_frame_cb;
    void                       *rx_frame_user;
    bm_uart_stats_t             stats;
    uint32_t                    rx_error_flags;
    size_t                      rx_since_event; /**< 上次帧事件以来接收字节数 */
    int                         initialized;
} bm_native_uart_state_t;

static bm_native_uart_state_t s_states[BM_NATIVE_UART_COUNT];
static uint8_t s_tx_buf[BM_NATIVE_UART_TX_BUF_SIZE];
static size_t  s_tx_count;

/**
 * @brief 由设备实例索引获取状态；无效时返回 NULL。
 */
static bm_native_uart_state_t *bm_native_uart_state_for(
    const struct bm_hal_uart *dev) {
    if (dev == &bm_uart_default) {
        return &s_states[0];
    }
    if (dev == &bm_native_uart1) {
        return &s_states[1];
    }
    return NULL;
}

/* -------------------------------------------------------------------------- */
/*  环形缓冲区操作                                                              */
/* -------------------------------------------------------------------------- */

static size_t bm_native_uart_ring_count(const bm_native_uart_ring_t *ring) {
    if (ring->head >= ring->tail) {
        return ring->head - ring->tail;
    }
    return ring->len - ring->tail + ring->head;
}

/**
 * @brief 将一个字节压入 UART 仿真环形队列。
 * @param ring UART 仿真环形队列。
 * @param c 待压入队列的字节。
 * @return 成功返回 BM_OK；设备未初始化时返回 BM_ERR_NOT_INIT；无可用静态槽位时返回 BM_ERR_NO_MEM。
 */
static int bm_native_uart_ring_push(bm_native_uart_ring_t *ring, uint8_t c) {
    size_t next;

    if (ring->len == 0u) {
        return BM_ERR_NOT_INIT; /* 未设置缓冲区 */
    }
    next = (ring->head + 1u) % ring->len;
    if (next == ring->tail) {
        return BM_ERR_NO_MEM; /* 满 */
    }
    ring->buf[ring->head] = c;
    ring->head = next;
    return BM_OK;
}

/**
 * @brief 从 UART 仿真环形队列弹出一个字节。
 * @param ring UART 仿真环形队列。
 * @param c 用于接收出队字节的输出指针；不得为 NULL。
 * @return 成功返回 BM_OK；队列为空或项目未找到时返回 BM_ERR_NOT_FOUND。
 */
static int bm_native_uart_ring_pop(bm_native_uart_ring_t *ring, uint8_t *c) {
    if (ring->head == ring->tail) {
        return BM_ERR_NOT_FOUND; /* 空 */
    }
    *c = ring->buf[ring->tail];
    ring->tail = (ring->tail + 1u) % ring->len;
    return BM_OK;
}

/* -------------------------------------------------------------------------- */
/*  测试辅助接口                                                                */
/* -------------------------------------------------------------------------- */

void bm_hal_uart_native_reset(void) {
    uint32_t i;

    for (i = 0u; i < BM_NATIVE_UART_COUNT; ++i) {
        (void)memset(&s_states[i], 0, sizeof(s_states[i]));
    }
    s_tx_count = 0u;
    (void)memset(s_tx_buf, 0, sizeof(s_tx_buf));
}

void bm_hal_uart_native_put_rx(const bm_hal_uart_t *dev, uint8_t c) {
    bm_native_uart_state_t *state;

    state = bm_native_uart_state_for(dev);
    if (state == NULL) {
        return;
    }

    if (state->rx_byte_cb != NULL) {
        state->rx_byte_cb(c);
    }

    if (bm_native_uart_ring_push(&state->rx_ring, c) != BM_OK) {
        state->stats.rx_overflow_count++;
        state->stats.last_errors |= BM_UART_ERR_OVERFLOW;
    } else {
        state->stats.rx_count++;
        state->rx_since_event++;
    }
}

void bm_hal_uart_native_put_rx_data(const bm_hal_uart_t *dev,
                                    const uint8_t *data, size_t len) {
    size_t i;

    if (data == NULL) {
        return;
    }
    for (i = 0u; i < len; ++i) {
        bm_hal_uart_native_put_rx(dev, data[i]);
    }
}

void bm_hal_uart_native_fire_idle(const bm_hal_uart_t *dev) {
    bm_native_uart_state_t *state;
    size_t len;

    state = bm_native_uart_state_for(dev);
    if (state == NULL || state->rx_frame_cb == NULL) {
        return;
    }
    len = state->rx_since_event;
    state->rx_since_event = 0u;
    state->rx_frame_cb(dev, BM_UART_EVT_IDLE, len, state->rx_frame_user);
}

void bm_hal_uart_native_inject_error(const bm_hal_uart_t *dev,
                                     uint32_t error) {
    bm_native_uart_state_t *state;

    state = bm_native_uart_state_for(dev);
    if (state == NULL) {
        return;
    }
    state->rx_error_flags |= error;
    state->stats.last_errors |= error;
    if ((error & BM_UART_ERR_OVERRUN) != 0u) {
        state->stats.rx_overrun_count++;
    }
    if ((error & BM_UART_ERR_FRAMING) != 0u) {
        state->stats.rx_framing_count++;
    }
    if ((error & BM_UART_ERR_PARITY) != 0u) {
        state->stats.rx_parity_count++;
    }
    if ((error & BM_UART_ERR_NOISE) != 0u) {
        state->stats.rx_noise_count++;
    }
}

size_t bm_hal_uart_native_tx_count(void) {
    return s_tx_count;
}

uint8_t bm_hal_uart_native_tx_byte(size_t idx) {
    if (idx >= s_tx_count) {
        return 0u;
    }
    return s_tx_buf[idx];
}

/* -------------------------------------------------------------------------- */
/*  driver API 实现                                                             */
/* -------------------------------------------------------------------------- */

static int native_uart_init(const struct bm_hal_uart *dev, void *config) {
    bm_native_uart_state_t *state;
    const bm_uart_config_t *cfg = (const bm_uart_config_t *)config;

    state = bm_native_uart_state_for(dev);
    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    (void)cfg; /* native 后端忽略波特率/校验等配置 */
    state->initialized = 1;
    return BM_OK;
}

/**
 * @brief 通过UART发送数据。
 * @param dev UART 设备实例。
 * @param data 待发送数据缓冲区。
 * @param len 缓冲区中的有效数据长度，单位为字节。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID；设备未初始化时返回 BM_ERR_NOT_INIT；资源忙或队列已满时返回 BM_ERR_BUSY。
 */
static int native_uart_send(const struct bm_hal_uart *dev,
                            const uint8_t *data, size_t len) {
    bm_native_uart_state_t *state;
    size_t i;

    state = bm_native_uart_state_for(dev);
    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    if (state->initialized == 0) {
        return BM_ERR_NOT_INIT;
    }
    if (data == NULL) {
        return BM_ERR_INVALID;
    }

    if (dev == &bm_uart_default) {
        /* 实例 0：console 输出 */
        (void)fwrite(data, 1, len, stdout);
        (void)fflush(stdout);
    } else {
        /* 实例 1：记录发送数据到测试缓冲区，不自动 loopback；满则报忙 */
        if (s_tx_count + len > BM_NATIVE_UART_TX_BUF_SIZE) {
            return BM_ERR_BUSY;
        }
        for (i = 0u; i < len; ++i) {
            s_tx_buf[s_tx_count++] = data[i];
        }
    }

    state->stats.tx_count += (uint32_t)len;

    if (state->tx_complete_cb != NULL) {
        state->tx_complete_cb(dev, state->tx_complete_user);
    }
    return BM_OK;
}

/**
 * @brief 从UART接收数据。
 * @param dev UART 设备实例。
 * @param data 接收数据缓冲区。
 * @param max_len 接收缓冲区容量，单位为字节。
 * @return 实际写入接收缓冲区的字节数；无数据或参数无效时返回 0。
 */
static size_t native_uart_recv(const struct bm_hal_uart *dev,
                               uint8_t *data, size_t max_len) {
    bm_native_uart_state_t *state;
    size_t n = 0u;

    state = bm_native_uart_state_for(dev);
    if (state == NULL || state->initialized == 0) {
        return 0u;
    }
    if (data == NULL || max_len == 0u) {
        return 0u;
    }

    while (n < max_len && bm_native_uart_ring_pop(&state->rx_ring, &data[n]) == BM_OK) {
        n++;
    }
    if (n > 0u && state->rx_since_event >= n) {
        state->rx_since_event -= n;
    } else if (n > 0u) {
        state->rx_since_event = 0u;
    }
    return n;
}

/**
 * @brief 设置UART接收回调。
 * @param dev UART 设备实例。
 * @param cb 接收回调；传入 NULL 时解除绑定。
 */
static void native_uart_set_rx_callback(const struct bm_hal_uart *dev,
                                        void (*cb)(uint8_t c)) {
    bm_native_uart_state_t *state;

    state = bm_native_uart_state_for(dev);
    if (state == NULL) {
        return;
    }
    state->rx_byte_cb = cb;
}

/**
 * @brief 中止UART当前传输。
 * @param dev UART 设备实例。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int native_uart_abort(const struct bm_hal_uart *dev) {
    bm_native_uart_state_t *state;

    state = bm_native_uart_state_for(dev);
    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    state->rx_ring.head = 0u;
    state->rx_ring.tail = 0u;
    return BM_OK;
}

/**
 * @brief 等待UART发送数据完成。
 * @param dev UART 设备实例；当前实现不使用该参数。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID；设备未初始化时返回 BM_ERR_NOT_INIT。
 */
static int native_uart_flush(const struct bm_hal_uart *dev) {
    bm_native_uart_state_t *state;

    state = bm_native_uart_state_for(dev);
    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    if (state->initialized == 0) {
        return BM_ERR_NOT_INIT;
    }
    (void)dev;
    return BM_OK;
}

/**
 * @brief 设置UART发送完成回调。
 * @param dev UART 设备实例。
 * @param cb 发送完成回调；传入 NULL 时解除绑定。
 * @param user 回调用户上下文，调用回调时原样传入。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int native_uart_set_tx_complete_callback(
    const struct bm_hal_uart *dev,
    bm_uart_tx_complete_callback_t cb, void *user) {
    bm_native_uart_state_t *state;

    state = bm_native_uart_state_for(dev);
    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    state->tx_complete_cb = cb;
    state->tx_complete_user = user;
    return BM_OK;
}

/**
 * @brief 设置UART接收帧回调。
 * @param dev UART 设备实例。
 * @param cb 接收帧回调；传入 NULL 时解除绑定。
 * @param user 回调用户上下文，调用回调时原样传入。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int native_uart_set_rx_frame_callback(
    const struct bm_hal_uart *dev,
    bm_uart_rx_frame_callback_t cb, void *user) {
    bm_native_uart_state_t *state;

    state = bm_native_uart_state_for(dev);
    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    state->rx_frame_cb = cb;
    state->rx_frame_user = user;
    return BM_OK;
}

/**
 * @brief 设置UART接收缓冲区。
 * @param dev UART 设备实例。
 * @param buf 待发送数据缓冲区。
 * @param len 缓冲区中的有效数据长度，单位为字节。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int native_uart_set_rx_buffer(const struct bm_hal_uart *dev,
                                     uint8_t *buf, size_t len) {
    bm_native_uart_state_t *state;

    state = bm_native_uart_state_for(dev);
    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    if (buf == NULL || len < 2u) {
        state->rx_ring.buf = NULL;
        state->rx_ring.len = 0u;
        state->rx_ring.head = 0u;
        state->rx_ring.tail = 0u;
        return (buf == NULL && len == 0u) ? BM_OK : BM_ERR_INVALID;
    }
    state->rx_ring.buf = buf;
    state->rx_ring.len = len;
    state->rx_ring.head = 0u;
    state->rx_ring.tail = 0u;
    return BM_OK;
}

/**
 * @brief 读取UART运行统计。
 * @param dev UART 设备实例。
 * @param stats 用于接收运行统计的输出结构；不得为 NULL。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int native_uart_get_stats(const struct bm_hal_uart *dev,
                                 bm_uart_stats_t *stats) {
    bm_native_uart_state_t *state;

    state = bm_native_uart_state_for(dev);
    if (state == NULL || stats == NULL) {
        return BM_ERR_INVALID;
    }
    *stats = state->stats;
    state->stats.last_errors = 0u; /* 读取后清零最近一次错误 */
    return BM_OK;
}

static const struct bm_uart_driver_api g_native_uart_api = {
    native_uart_init,
    native_uart_send,
    native_uart_recv,
    native_uart_set_rx_callback,
    native_uart_abort,
    native_uart_flush,
    native_uart_set_tx_complete_callback,
    native_uart_set_rx_frame_callback,
    native_uart_set_rx_buffer,
    native_uart_get_stats,
};

const bm_hal_uart_t bm_uart_default = { &g_native_uart_api, NULL };
const bm_hal_uart_t bm_native_uart1 = { &g_native_uart_api, NULL };
