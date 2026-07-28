/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_rs485.c
 * @brief RS485 半双工链路包装组件实现
 *
 * 架在 UART HAL 与 GPIO HAL 之上，负责 DE 方向控制、发送前后保持、
 * 接收帧事件、半双工冲突检测与链路统计。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 RS485 包装组件
 */
#include "bm/component/bm_rs485.h"

#include "bm/common/bm_uptime.h"

#include <string.h>

/**
 * @brief 设置 DE 电平到指定方向。
 */
static void bm_rs485_set_de(bm_rs485_t *rs485, uint32_t dir) {
    int de_value;

    if (rs485->config.de_gpio == NULL || rs485->config.hardware_de != 0) {
        return;
    }

    de_value = (dir == BM_RS485_DIR_RX)
                   ? (rs485->config.de_active_high ? 0 : 1)
                   : (rs485->config.de_active_high ? 1 : 0);
    (void)bm_hal_gpio_write(rs485->config.de_gpio,
                            rs485->config.de_pin, de_value);
}

/**
 * @brief UART TX 完成回调：启动 post_delay，切换到 TX_TAIL。
 */
static void bm_rs485_tx_complete_cb(const bm_hal_uart_t *dev, void *user) {
    bm_rs485_t *rs485 = (bm_rs485_t *)user;

    (void)dev;
    if (rs485 == NULL) {
        return;
    }
    if (rs485->state.dir == BM_RS485_DIR_TX) {
        rs485->state.tx_end_us = bm_uptime_us();
        rs485->state.dir = BM_RS485_DIR_TX_TAIL;
    }
}

/**
 * @brief UART RX 帧/IDLE 回调：提取帧、过滤回显、检测冲突。
 */
static void bm_rs485_rx_frame_cb(const bm_hal_uart_t *dev, uint32_t event,
                                 size_t len, void *user) {
    bm_rs485_t *rs485 = (bm_rs485_t *)user;
    uint8_t tmp[BM_RS485_MAX_FRAME_LEN];
    size_t  got;
    size_t  i;
    size_t  consume;

    (void)dev;
    if (rs485 == NULL || rs485->config.uart == NULL) {
        return;
    }

    if (event != BM_UART_EVT_IDLE && event != BM_UART_EVT_FRAME_END
        && event != BM_UART_EVT_RX_FULL) {
        return;
    }

    if (len == 0u) {
        return;
    }

    if (len > BM_RS485_MAX_FRAME_LEN) {
        /* 帧过长：丢弃 */
        (void)bm_hal_uart_recv(rs485->config.uart, tmp, sizeof(tmp));
        rs485->state.stats.frame_drop_count++;
        rs485->state.stats.last_errors |= BM_RS485_ERR_FRAME_DROP;
        if (rs485->resources.error_cb != NULL) {
            rs485->resources.error_cb((const bm_rs485_t *)rs485,
                                      BM_RS485_ERR_FRAME_DROP,
                                      rs485->resources.user);
        }
        return;
    }

    got = bm_hal_uart_recv(rs485->config.uart, tmp, len);
    if (got == 0u) {
        return;
    }

    rs485->state.last_rx_us = bm_uptime_us();

    /* 回显过滤：已发送数据的回显排在接收数据前部，按 echo_len 跳过 */
    if (rs485->config.filter_echo != 0 && rs485->state.echo_pending != 0) {
        consume = (got < rs485->state.echo_len) ? got : rs485->state.echo_len;
        rs485->state.echo_len -= consume;
        if (rs485->state.echo_len == 0u) {
            rs485->state.echo_pending = 0;
        }
    } else {
        consume = 0u;
    }

    /* 非回显数据写入帧缓冲 */
    for (i = consume; i < got; ++i) {
        if (rs485->state.rx_len >= BM_RS485_MAX_FRAME_LEN) {
            rs485->state.stats.frame_drop_count++;
            rs485->state.stats.last_errors |= BM_RS485_ERR_FRAME_DROP;
            if (rs485->resources.error_cb != NULL) {
                rs485->resources.error_cb((const bm_rs485_t *)rs485,
                                          BM_RS485_ERR_FRAME_DROP,
                                          rs485->resources.user);
            }
            rs485->state.rx_len = 0u;
            return;
        }
        rs485->state.rx_buf[rs485->state.rx_len++] = tmp[i];
    }

    /* 发送期间或尾保持期间收到非回显数据视为冲突 */
    if ((rs485->state.dir == BM_RS485_DIR_TX
         || rs485->state.dir == BM_RS485_DIR_TX_TAIL)
        && rs485->state.rx_len > 0u) {
        rs485->state.stats.collision_count++;
        rs485->state.stats.last_errors |= BM_RS485_ERR_COLLISION;
        if (rs485->resources.error_cb != NULL) {
            rs485->resources.error_cb((const bm_rs485_t *)rs485,
                                      BM_RS485_ERR_COLLISION,
                                      rs485->resources.user);
        }
        rs485->state.rx_len = 0u;
        return;
    }

    if (rs485->state.rx_len > 0u
        && rs485->resources.frame_rx_cb != NULL) {
        rs485->state.stats.rx_frame_count++;
        rs485->state.stats.rx_byte_count += (uint32_t)rs485->state.rx_len;
        rs485->resources.frame_rx_cb((const bm_rs485_t *)rs485,
                                     rs485->state.rx_buf,
                                     rs485->state.rx_len,
                                     rs485->resources.user);
    }
    rs485->state.rx_len = 0u;
}

/* -------------------------------------------------------------------------- */
/*  公开 API                                                                   */
/* -------------------------------------------------------------------------- */

int bm_rs485_validate_config(const bm_rs485_config_t *config) {
    if (config == NULL || config->uart == NULL) {
        return BM_ERR_INVALID;
    }
    if (config->hardware_de == 0 && config->de_gpio == NULL) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

int bm_rs485_init(bm_rs485_t *rs485) {
    int rc;

    if (rs485 == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_rs485_validate_config(&rs485->config) != BM_OK) {
        return BM_ERR_INVALID;
    }

    (void)memset(&rs485->state, 0, sizeof(rs485->state));
    rs485->state.dir = BM_RS485_DIR_RX;

    rc = bm_hal_uart_init(rs485->config.uart, NULL);
    if (rc != BM_OK && rc != BM_ERR_NOT_INIT) {
        return rc;
    }

    /* 配置 UART ring buffer 为本机帧缓冲 */
    (void)bm_hal_uart_set_rx_buffer(rs485->config.uart,
                                    rs485->state.rx_buf,
                                    sizeof(rs485->state.rx_buf));

    /* 注册回调 */
    (void)bm_hal_uart_set_tx_complete_callback(
        rs485->config.uart, bm_rs485_tx_complete_cb, rs485);
    (void)bm_hal_uart_set_rx_frame_callback(
        rs485->config.uart, bm_rs485_rx_frame_cb, rs485);

    /* DE 初始化为接收方向 */
    bm_rs485_set_de(rs485, BM_RS485_DIR_RX);

    return BM_OK;
}

void bm_rs485_reset(bm_rs485_t *rs485) {
    if (rs485 == NULL) {
        return;
    }
    rs485->state.dir = BM_RS485_DIR_RX;
    rs485->state.rx_len = 0u;
    rs485->state.echo_pending = 0;
    rs485->state.echo_len = 0u;
    rs485->state.tx_end_us = 0u;
    (void)memset(&rs485->state.stats, 0, sizeof(rs485->state.stats));
    bm_rs485_set_de(rs485, BM_RS485_DIR_RX);
    (void)bm_hal_uart_abort(rs485->config.uart);
}

int bm_rs485_send(bm_rs485_t *rs485, const uint8_t *data, size_t len) {
    int rc;

    if (rs485 == NULL || data == NULL) {
        return BM_ERR_INVALID;
    }
    if (len == 0u) {
        return BM_OK;
    }
    if (rs485->state.dir != BM_RS485_DIR_RX) {
        return BM_ERR_BUSY;
    }

    rs485->state.dir = BM_RS485_DIR_TX;
    rs485->state.rx_len = 0u;

    if (rs485->config.filter_echo != 0) {
        rs485->state.echo_pending = 1;
        rs485->state.echo_len = len;
    }

    bm_rs485_set_de(rs485, BM_RS485_DIR_TX);

    rc = bm_hal_uart_send(rs485->config.uart, data, len);
    if (rc != BM_OK) {
        rs485->state.dir = BM_RS485_DIR_RX;
        bm_rs485_set_de(rs485, BM_RS485_DIR_RX);
        rs485->state.echo_pending = 0;
        rs485->state.echo_len = 0u;
        return rc;
    }

    rs485->state.stats.tx_frame_count++;
    rs485->state.stats.tx_byte_count += (uint32_t)len;
    return BM_OK;
}

void bm_rs485_poll(bm_rs485_t *rs485) {
    uint64_t now;

    if (rs485 == NULL) {
        return;
    }

    /* TX_TAIL：等待 post_delay 后切回 RX */
    if (rs485->state.dir == BM_RS485_DIR_TX_TAIL) {
        now = bm_uptime_us();
        if ((now - rs485->state.tx_end_us) >= rs485->config.post_delay_us) {
            rs485->state.dir = BM_RS485_DIR_RX;
            bm_rs485_set_de(rs485, BM_RS485_DIR_RX);
        }
    }

    /* RX 空闲超时检测 */
    if (rs485->config.rx_idle_timeout_us != 0u
        && rs485->state.dir == BM_RS485_DIR_RX
        && rs485->state.stats.rx_byte_count > 0u) {
        now = bm_uptime_us();
        if ((now - rs485->state.last_rx_us)
            >= rs485->config.rx_idle_timeout_us) {
            rs485->state.stats.rx_idle_timeout_count++;
            rs485->state.stats.last_errors |= BM_RS485_ERR_RX_IDLE_TIMEOUT;
            rs485->state.last_rx_us = now;
            if (rs485->resources.error_cb != NULL) {
                rs485->resources.error_cb((const bm_rs485_t *)rs485,
                    BM_RS485_ERR_RX_IDLE_TIMEOUT, rs485->resources.user);
            }
        }
    }
}

int bm_rs485_get_stats(const bm_rs485_t *rs485, bm_rs485_stats_t *stats) {
    if (rs485 == NULL || stats == NULL) {
        return BM_ERR_INVALID;
    }
    *stats = rs485->state.stats;
    return BM_OK;
}

uint32_t bm_rs485_dir(const bm_rs485_t *rs485) {
    if (rs485 == NULL) {
        return BM_RS485_DIR_RX;
    }
    return rs485->state.dir;
}
