/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_rs485.c
 * @brief RS485 半双工链路包装组件实现
 *
 * 架在 UART HAL 与 GPIO HAL 之上，负责 DE 方向控制、发送前后保持、
 * 接收帧事件、半双工冲突检测与链路统计。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 RS485 包装组件
 * 2026-07-28       1.1            zeh            审查整改：独立帧拼装缓冲消除越界/覆盖、UART 错误粘滞位去重、TX_PRE 冲突判定、TX 超时回退、时间戳读取临界区
 */
#include "bm/component/bm_rs485.h"

#include "bm/common/bm_uptime.h"
#include "bm/common/bm_critical_wrap.h"

#include <string.h>

/**
 * @brief 设置 DE 电平到指定方向。
 *
 * @return 0 成功；非零 GPIO 写入失败
 */
static int bm_rs485_set_de(bm_rs485_t *rs485, uint32_t dir) {
    int de_value;
    int rc;

    if (rs485->config.de_gpio == NULL || rs485->config.hardware_de != 0) {
        return 0;
    }

    de_value = (dir == BM_RS485_DIR_RX)
                   ? (rs485->config.de_active_high ? 0 : 1)
                   : (rs485->config.de_active_high ? 1 : 0);
    rc = bm_hal_gpio_write(rs485->config.de_gpio,
                           rs485->config.de_pin, de_value);
    if (rc != BM_OK) {
        rs485->state.stats.last_errors |= BM_RS485_ERR_GPIO;
        if (rs485->resources.error_cb != NULL) {
            rs485->resources.error_cb((const bm_rs485_t *)rs485,
                                      BM_RS485_ERR_GPIO,
                                      rs485->resources.user);
        }
    }
    return rc;
}

/**
 * @brief 清除发送相关暂存与回显过滤状态。
 */
static void bm_rs485_clear_tx_echo(bm_rs485_t *rs485) {
    rs485->state.tx_data = NULL;
    rs485->state.tx_len = 0u;
    rs485->state.echo_pending = 0;
    rs485->state.echo_buf_ptr = NULL;
    rs485->state.echo_len = 0u;
    rs485->state.echo_offset = 0u;
}

/**
 * @brief UART TX 完成回调：启动 post_delay，切换到 TX_TAIL。
 */
static void bm_rs485_tx_complete_cb(const bm_hal_uart_t *dev, void *user) {
    bm_rs485_t *rs485 = (bm_rs485_t *)user;
    bm_irq_state_t irq_state;
    uint64_t now;

    (void)dev;
    if (rs485 == NULL) {
        return;
    }

    now = bm_uptime_us();
    irq_state = BM_CRITICAL_ENTER();
    if (rs485->state.dir == BM_RS485_DIR_TX) {
        rs485->state.tx_end_us = now;
        rs485->state.dir = BM_RS485_DIR_TX_TAIL;
    }
    BM_CRITICAL_EXIT(irq_state);
}

/**
 * @brief UART RX 帧/IDLE 回调：提取帧、过滤回显、检测冲突。
 *
 * 帧拼装在 state.rx_frame_buf 内进行（帧长上限 sizeof(rx_frame_buf)），
 * state.rx_buf_ptr 专职 HAL 环形存储，二者不复用，避免环形槽位被后端
 * 复写后覆盖已拼装帧数据。
 */
static void bm_rs485_rx_frame_cb(const bm_hal_uart_t *dev, uint32_t event,
                                 size_t len, void *user) {
    bm_rs485_t *rs485 = (bm_rs485_t *)user;
    size_t  got;
    size_t  i;
    bm_irq_state_t irq_state;
    uint64_t now;
    int      collision = 0;
    int      frame_drop = 0;
    size_t   frame_len = 0u;

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

    /* 帧过长（超过内部拼装缓冲上限）：分块排空环形缓冲后丢弃并更新统计 */
    if (len > sizeof(rs485->state.rx_frame_buf)) {
        size_t remain = len;

        while (remain > 0u) {
            size_t chunk = (remain > sizeof(rs485->state.rx_frame_buf))
                               ? sizeof(rs485->state.rx_frame_buf) : remain;
            size_t drained = bm_hal_uart_recv(rs485->config.uart,
                                              rs485->state.rx_frame_buf,
                                              chunk);
            if (drained == 0u) {
                break;
            }
            remain -= drained;
        }
        irq_state = BM_CRITICAL_ENTER();
        rs485->state.stats.frame_drop_count++;
        rs485->state.stats.last_errors |= BM_RS485_ERR_FRAME_DROP;
        BM_CRITICAL_EXIT(irq_state);
        if (rs485->resources.error_cb != NULL) {
            rs485->resources.error_cb((const bm_rs485_t *)rs485,
                                      BM_RS485_ERR_FRAME_DROP,
                                      rs485->resources.user);
        }
        return;
    }

    got = bm_hal_uart_recv(rs485->config.uart,
                           rs485->state.rx_frame_buf, len);
    if (got == 0u) {
        return;
    }

    now = bm_uptime_us();

    irq_state = BM_CRITICAL_ENTER();
    rs485->state.last_rx_us = now;
    rs485->state.rx_idle_timeout_fired = 0;

    /* 逐字节过滤回显：与已发送数据逐字节比较，支持跨多次 DMA 事件；
       非回显字节在 rx_frame_buf 内原地压缩拼装（rx_len 恒不大于 i） */
    for (i = 0u; i < got; ++i) {
        if (rs485->config.filter_echo != 0
            && rs485->state.echo_pending != 0
            && rs485->state.echo_len > 0u
            && rs485->state.rx_frame_buf[i]
                   == rs485->state.echo_buf_ptr[rs485->state.echo_offset]) {
            rs485->state.echo_offset++;
            rs485->state.echo_len--;
            if (rs485->state.echo_len == 0u) {
                rs485->state.echo_pending = 0;
            }
            continue;
        }

        /* 非回显字节：停止回显过滤并写入帧缓冲 */
        rs485->state.echo_pending = 0;
        if (rs485->state.rx_len >= sizeof(rs485->state.rx_frame_buf)) {
            frame_drop = 1;
            rs485->state.stats.frame_drop_count++;
            rs485->state.stats.last_errors |= BM_RS485_ERR_FRAME_DROP;
            rs485->state.rx_len = 0u;
            break;
        }
        rs485->state.rx_frame_buf[rs485->state.rx_len++] =
            rs485->state.rx_frame_buf[i];
    }

    /* 发送前置/发送中/尾保持期间收到非回显数据视为冲突 */
    if (!frame_drop
        && (rs485->state.dir == BM_RS485_DIR_TX_PRE
            || rs485->state.dir == BM_RS485_DIR_TX
            || rs485->state.dir == BM_RS485_DIR_TX_TAIL)
        && rs485->state.rx_len > 0u) {
        collision = 1;
        rs485->state.stats.collision_count++;
        rs485->state.stats.last_errors |= BM_RS485_ERR_COLLISION;
        rs485->state.rx_len = 0u;
    }

    if (!frame_drop && !collision && rs485->state.rx_len > 0u) {
        rs485->state.stats.rx_frame_count++;
        rs485->state.stats.rx_byte_count += (uint32_t)rs485->state.rx_len;
        frame_len = rs485->state.rx_len;
    }
    BM_CRITICAL_EXIT(irq_state);

    /* 回调在临界区外调用，避免阻塞其他中断 */
    if (frame_drop != 0 && rs485->resources.error_cb != NULL) {
        rs485->resources.error_cb((const bm_rs485_t *)rs485,
                                  BM_RS485_ERR_FRAME_DROP,
                                  rs485->resources.user);
    }
    if (collision != 0 && rs485->resources.error_cb != NULL) {
        rs485->resources.error_cb((const bm_rs485_t *)rs485,
                                  BM_RS485_ERR_COLLISION,
                                  rs485->resources.user);
    }
    if (frame_len > 0u && rs485->resources.frame_rx_cb != NULL) {
        rs485->resources.frame_rx_cb((const bm_rs485_t *)rs485,
                                     rs485->state.rx_frame_buf,
                                     frame_len,
                                     rs485->resources.user);
    }

    irq_state = BM_CRITICAL_ENTER();
    rs485->state.rx_len = 0u;
    BM_CRITICAL_EXIT(irq_state);
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

    /* 配置 UART ring buffer：优先使用 App 提供的外部缓冲 */
    if (rs485->config.rx_buf != NULL && rs485->config.rx_buf_len > 0u) {
        rs485->state.rx_buf_ptr = rs485->config.rx_buf;
        rs485->state.rx_buf_len = rs485->config.rx_buf_len;
    } else {
        rs485->state.rx_buf_ptr = rs485->state.rx_internal_buf;
        rs485->state.rx_buf_len = sizeof(rs485->state.rx_internal_buf);
    }
    rc = bm_hal_uart_set_rx_buffer(rs485->config.uart,
                                   rs485->state.rx_buf_ptr,
                                   rs485->state.rx_buf_len);
    if (rc != BM_OK) {
        return rc;
    }

    /* 注册回调 */
    rc = bm_hal_uart_set_tx_complete_callback(
        rs485->config.uart, bm_rs485_tx_complete_cb, rs485);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_hal_uart_set_rx_frame_callback(
        rs485->config.uart, bm_rs485_rx_frame_cb, rs485);
    if (rc != BM_OK) {
        (void)bm_hal_uart_set_tx_complete_callback(
            rs485->config.uart, NULL, NULL);
        return rc;
    }

    /* DE 初始化为接收方向；软件 DE 时先把 GPIO 配置为输出 */
    if (rs485->config.hardware_de == 0 && rs485->config.de_gpio != NULL) {
        rc = bm_hal_gpio_configure(rs485->config.de_gpio,
                                   rs485->config.de_pin,
                                   BM_GPIO_OUTPUT);
        if (rc != BM_OK) {
            (void)bm_hal_uart_set_rx_frame_callback(
                rs485->config.uart, NULL, NULL);
            (void)bm_hal_uart_set_tx_complete_callback(
                rs485->config.uart, NULL, NULL);
            return rc;
        }
    }
    rc = bm_rs485_set_de(rs485, BM_RS485_DIR_RX);
    if (rc != BM_OK) {
        (void)bm_hal_uart_set_rx_frame_callback(
            rs485->config.uart, NULL, NULL);
        (void)bm_hal_uart_set_tx_complete_callback(
            rs485->config.uart, NULL, NULL);
        return rc;
    }

    return BM_OK;
}

void bm_rs485_reset(bm_rs485_t *rs485) {
    bm_irq_state_t irq_state;

    if (rs485 == NULL) {
        return;
    }

    irq_state = BM_CRITICAL_ENTER();
    rs485->state.dir = BM_RS485_DIR_RX;
    rs485->state.rx_len = 0u;
    rs485->state.tx_data = NULL;
    rs485->state.tx_len = 0u;
    rs485->state.tx_pre_start_us = 0u;
    bm_rs485_clear_tx_echo(rs485);
    rs485->state.tx_end_us = 0u;
    (void)memset(&rs485->state.stats, 0, sizeof(rs485->state.stats));
    BM_CRITICAL_EXIT(irq_state);

    /* 中止 UART 并强制 DE 回到接收方向 */
    (void)bm_hal_uart_abort(rs485->config.uart);
    (void)bm_rs485_set_de(rs485, BM_RS485_DIR_RX);
}

int bm_rs485_send(bm_rs485_t *rs485, const uint8_t *data, size_t len) {
    int rc;
    bm_irq_state_t irq_state;
    uint64_t now;

    if (rs485 == NULL || data == NULL) {
        return BM_ERR_INVALID;
    }
    if (len == 0u) {
        return BM_OK;
    }

    irq_state = BM_CRITICAL_ENTER();
    if (rs485->state.dir != BM_RS485_DIR_RX) {
        BM_CRITICAL_EXIT(irq_state);
        return BM_ERR_BUSY;
    }

    rs485->state.rx_len = 0u;

    /* 记录发送数据，供 TX_PRE 启动和回显过滤使用 */
    rs485->state.tx_data = data;
    rs485->state.tx_len = len;

    if (rs485->config.filter_echo != 0) {
        rs485->state.echo_pending = 1;
        rs485->state.echo_buf_ptr = data;
        rs485->state.echo_len = len;
        rs485->state.echo_offset = 0u;
    }

    /* 先占用方向，再退出临界区执行 GPIO/UART 操作 */
    now = bm_uptime_us();
    if (rs485->config.pre_delay_us == 0u) {
        rs485->state.dir = BM_RS485_DIR_TX;
        rs485->state.tx_start_us = now;
    } else {
        rs485->state.dir = BM_RS485_DIR_TX_PRE;
        rs485->state.tx_pre_start_us = now;
    }
    BM_CRITICAL_EXIT(irq_state);

    rc = bm_rs485_set_de(rs485, BM_RS485_DIR_TX);
    if (rc != BM_OK) {
        irq_state = BM_CRITICAL_ENTER();
        rs485->state.dir = BM_RS485_DIR_RX;
        bm_rs485_clear_tx_echo(rs485);
        BM_CRITICAL_EXIT(irq_state);
        return rc;
    }

    /* 无前置保持时间：立即启动 UART 发送 */
    if (rs485->config.pre_delay_us == 0u) {
        rc = bm_hal_uart_send(rs485->config.uart, data, len);
        if (rc != BM_OK) {
            irq_state = BM_CRITICAL_ENTER();
            rs485->state.dir = BM_RS485_DIR_RX;
            bm_rs485_clear_tx_echo(rs485);
            BM_CRITICAL_EXIT(irq_state);
            (void)bm_rs485_set_de(rs485, BM_RS485_DIR_RX);
            return rc;
        }

        irq_state = BM_CRITICAL_ENTER();
        rs485->state.stats.tx_frame_count++;
        rs485->state.stats.tx_byte_count += (uint32_t)len;
        BM_CRITICAL_EXIT(irq_state);
    }

    return BM_OK;
}

void bm_rs485_poll(bm_rs485_t *rs485) {
    uint64_t now;
    bm_uart_stats_t uart_stats;
    int rc;
    bm_irq_state_t irq_state;
    uint32_t dir;
    const uint8_t *tx_data;
    size_t tx_len;
    uint64_t pre_start_us = 0u;
    uint64_t tx_end_us = 0u;
    int      tx_timed_out = 0;
    uint32_t new_uart_errors = 0u;

    if (rs485 == NULL) {
        return;
    }

    now = bm_uptime_us();

    irq_state = BM_CRITICAL_ENTER();
    dir = rs485->state.dir;
    BM_CRITICAL_EXIT(irq_state);

    /* TX_PRE：等待 pre_delay 到期后启动 UART 发送 */
    if (dir == BM_RS485_DIR_TX_PRE) {
        irq_state = BM_CRITICAL_ENTER();
        tx_data = rs485->state.tx_data;
        tx_len = rs485->state.tx_len;
        pre_start_us = rs485->state.tx_pre_start_us;
        BM_CRITICAL_EXIT(irq_state);

        if ((now - pre_start_us)
            >= rs485->config.pre_delay_us) {
            irq_state = BM_CRITICAL_ENTER();
            rs485->state.dir = BM_RS485_DIR_TX;
            rs485->state.tx_start_us = now;
            BM_CRITICAL_EXIT(irq_state);

            rc = bm_hal_uart_send(rs485->config.uart, tx_data, tx_len);
            if (rc != BM_OK) {
                irq_state = BM_CRITICAL_ENTER();
                rs485->state.dir = BM_RS485_DIR_RX;
                bm_rs485_clear_tx_echo(rs485);
                rs485->state.stats.last_errors |= BM_RS485_ERR_UART;
                BM_CRITICAL_EXIT(irq_state);
                (void)bm_rs485_set_de(rs485, BM_RS485_DIR_RX);
                if (rs485->resources.error_cb != NULL) {
                    rs485->resources.error_cb((const bm_rs485_t *)rs485,
                                              BM_RS485_ERR_UART,
                                              rs485->resources.user);
                }
            } else {
                irq_state = BM_CRITICAL_ENTER();
                rs485->state.stats.tx_frame_count++;
                rs485->state.stats.tx_byte_count += (uint32_t)tx_len;
                BM_CRITICAL_EXIT(irq_state);
            }
        }
    }

    /* TX_TAIL：等待 post_delay 后切回 RX */
    irq_state = BM_CRITICAL_ENTER();
    dir = rs485->state.dir;
    tx_end_us = rs485->state.tx_end_us;
    BM_CRITICAL_EXIT(irq_state);
    if (dir == BM_RS485_DIR_TX_TAIL) {
        if ((now - tx_end_us) >= rs485->config.post_delay_us) {
            irq_state = BM_CRITICAL_ENTER();
            rs485->state.dir = BM_RS485_DIR_RX;
            BM_CRITICAL_EXIT(irq_state);
            (void)bm_rs485_set_de(rs485, BM_RS485_DIR_RX);
        }
    }

    /* TX/TX_TAIL 超时：TC 回调丢失时回退 RX，避免永久 BM_ERR_BUSY */
    if (rs485->config.tx_timeout_us != 0u) {
        uint64_t tx_start_us;

        irq_state = BM_CRITICAL_ENTER();
        dir = rs485->state.dir;
        tx_start_us = rs485->state.tx_start_us;
        if ((dir == BM_RS485_DIR_TX || dir == BM_RS485_DIR_TX_TAIL)
            && (now - tx_start_us) >= rs485->config.tx_timeout_us) {
            rs485->state.dir = BM_RS485_DIR_RX;
            bm_rs485_clear_tx_echo(rs485);
            rs485->state.stats.tx_timeout_count++;
            rs485->state.stats.last_errors |= BM_RS485_ERR_TX_TIMEOUT;
            tx_timed_out = 1;
        }
        BM_CRITICAL_EXIT(irq_state);
        if (tx_timed_out != 0) {
            (void)bm_rs485_set_de(rs485, BM_RS485_DIR_RX);
            if (rs485->resources.error_cb != NULL) {
                rs485->resources.error_cb((const bm_rs485_t *)rs485,
                                          BM_RS485_ERR_TX_TIMEOUT,
                                          rs485->resources.user);
            }
        }
    }

    /* UART 底层错误透传：last_errors 为粘滞位，仅对新增位上报一次 */
    if (bm_hal_uart_get_stats(rs485->config.uart, &uart_stats) == BM_OK) {
        irq_state = BM_CRITICAL_ENTER();
        new_uart_errors = uart_stats.last_errors
                          & ~rs485->state.uart_err_reported;
        rs485->state.uart_err_reported = uart_stats.last_errors;
        if (new_uart_errors != 0u) {
            rs485->state.stats.last_errors |= BM_RS485_ERR_UART;
        }
        BM_CRITICAL_EXIT(irq_state);
        if (new_uart_errors != 0u && rs485->resources.error_cb != NULL) {
            rs485->resources.error_cb((const bm_rs485_t *)rs485,
                                      BM_RS485_ERR_UART,
                                      rs485->resources.user);
        }
    }

    /* RX 空闲超时检测：只触发一次，新数据到达后由 rx_frame_cb 复位 */
    irq_state = BM_CRITICAL_ENTER();
    if (rs485->config.rx_idle_timeout_us != 0u
        && rs485->state.dir == BM_RS485_DIR_RX
        && rs485->state.rx_idle_timeout_fired == 0
        && rs485->state.stats.rx_byte_count > 0u
        && (now - rs485->state.last_rx_us)
           >= rs485->config.rx_idle_timeout_us) {
        rs485->state.rx_idle_timeout_fired = 1;
        rs485->state.stats.rx_idle_timeout_count++;
        rs485->state.stats.last_errors |= BM_RS485_ERR_RX_IDLE_TIMEOUT;
        BM_CRITICAL_EXIT(irq_state);
        if (rs485->resources.error_cb != NULL) {
            rs485->resources.error_cb((const bm_rs485_t *)rs485,
                BM_RS485_ERR_RX_IDLE_TIMEOUT, rs485->resources.user);
        }
    } else {
        BM_CRITICAL_EXIT(irq_state);
    }
}

int bm_rs485_get_stats(const bm_rs485_t *rs485, bm_rs485_stats_t *stats) {
    bm_irq_state_t irq_state;

    if (rs485 == NULL || stats == NULL) {
        return BM_ERR_INVALID;
    }
    irq_state = BM_CRITICAL_ENTER();
    *stats = rs485->state.stats;
    BM_CRITICAL_EXIT(irq_state);
    return BM_OK;
}

uint32_t bm_rs485_dir(const bm_rs485_t *rs485) {
    bm_irq_state_t irq_state;
    uint32_t dir;

    if (rs485 == NULL) {
        return BM_RS485_DIR_RX;
    }
    irq_state = BM_CRITICAL_ENTER();
    dir = rs485->state.dir;
    BM_CRITICAL_EXIT(irq_state);
    return dir;
}
