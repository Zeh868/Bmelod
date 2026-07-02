/**
 * @file transport_qos.c
 * @brief 传输延迟与抖动监控实现
 *
 * 基于 on_tx/on_rx 钩子测量单程延迟，EMA 滤波后触发报警；
 * 同时集成 token bucket 令牌桶对出队字节进行整形。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            补 SPDX 与函数级 Doxygen
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/component/transport_qos.h"
#include "bm/algorithm/bm_algo_common.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

/**
 * @brief 校验 QoS 配置参数
 *
 * @param config 配置结构指针（不可为 NULL）
 * @return BM_OK 合法；BM_ERR_INVALID 参数无效（ema_alpha 超范围）
 */
int bm_transport_qos_validate_config(const bm_transport_qos_config_t *config) {
    if (config == NULL || config->ema_alpha <= 0.0f ||
        config->ema_alpha > 1.0f) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

/**
 * @brief 复位 QoS 状态（清零延迟/抖动/令牌与遥测）
 *
 * @param axis QoS 轴实例（NULL 时直接返回）
 */
void bm_transport_qos_reset(bm_transport_qos_axis_t *axis) {
    if (axis == NULL) {
        return;
    }

    axis->state.prev_tx_ms = 0u;
    axis->state.latency_ms = 0.0f;
    axis->state.jitter_ms = 0.0f;
    axis->state.latency_ema_ms = 0.0f;
    axis->state.tokens = 0.0f;
    axis->state.last_token_ms = 0u;
    axis->state.have_token_time = 0;
    axis->state.have_prev = 0;
    axis->state.step_count = 0u;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
}

/**
 * @brief 初始化 QoS 轴（校验配置后复位，并按 burst 容量预填令牌）
 *
 * @param axis QoS 轴实例（不可为 NULL；config 须预先填写）
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效
 */
int bm_transport_qos_init(bm_transport_qos_axis_t *axis) {
    if (axis == NULL ||
        bm_transport_qos_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_transport_qos_reset(axis);
    if (axis->config.token_burst_bytes > 0u) {
        axis->state.tokens = (float)axis->config.token_burst_bytes;
    }
    return BM_OK;
}

/**
 * @brief 记录当前时刻为发送时间戳（在数据包发出时调用）
 *
 * @param axis QoS 轴实例（NULL 或无 now_ms 回调时直接返回）
 */
void bm_transport_qos_on_tx(bm_transport_qos_axis_t *axis) {
    if (axis == NULL || axis->resources.now_ms == NULL) {
        return;
    }
    axis->state.prev_tx_ms =
        axis->resources.now_ms(axis->resources.now_ms_user);
}

/**
 * @brief 在收到数据包时计算延迟与抖动，并更新 EMA（在数据包到达时调用）
 *
 * @param axis QoS 轴实例（NULL 或无 now_ms 回调时直接返回）
 */
void bm_transport_qos_on_rx(bm_transport_qos_axis_t *axis) {
    const bm_transport_qos_config_t *cfg;
    float latency;
    float jitter;
    uint32_t now_ms;

    if (axis == NULL || axis->resources.now_ms == NULL) {
        return;
    }

    cfg = &axis->config;
    now_ms = axis->resources.now_ms(axis->resources.now_ms_user);

    if (axis->state.prev_tx_ms == 0u) {
        return;
    }

    latency = (float)(int32_t)(now_ms - axis->state.prev_tx_ms);
    if (axis->state.have_prev) {
        jitter = fabsf(latency - axis->state.latency_ms);
    } else {
        jitter = 0.0f;
        axis->state.have_prev = 1;
    }

    axis->state.latency_ms = latency;
    axis->state.jitter_ms = jitter;
    axis->state.latency_ema_ms =
        cfg->ema_alpha * latency +
        (1.0f - cfg->ema_alpha) * axis->state.latency_ema_ms;
    axis->state.prev_tx_ms = 0u;
}

/**
 * @brief 按经过时间补充令牌桶（静态辅助）
 *
 * 依据 token_rate_bytes_per_ms × 自上次补充经过的毫秒数补充令牌，上限
 * token_burst_bytes。时间差用无符号回绕安全减法计算（now - last，49.7 天内
 * 单调有效），首次调用仅登记基准时刻不补充。令牌速率/桶容量未配置或缺
 * now_ms 回调时为空操作（P0-5c：此前令牌只扣不补，桶空后永久拒绝入队）。
 *
 * @param axis QoS 轴实例（不可为 NULL）
 */
static void token_refill(bm_transport_qos_axis_t *axis) {
    const bm_transport_qos_config_t *cfg = &axis->config;
    bm_transport_qos_state_t *st = &axis->state;
    uint32_t now_ms;
    uint32_t elapsed_ms;
    float burst;
    float added;

    if (cfg->token_rate_bytes_per_ms <= 0.0f ||
        cfg->token_burst_bytes == 0u ||
        axis->resources.now_ms == NULL) {
        return;
    }

    now_ms = axis->resources.now_ms(axis->resources.now_ms_user);

    if (!st->have_token_time) {
        st->last_token_ms = now_ms;
        st->have_token_time = 1;
        return;
    }

    /* 无符号回绕安全差：now 早于 last 亦得正确经过量（模 2^32） */
    elapsed_ms = now_ms - st->last_token_ms;
    if (elapsed_ms == 0u) {
        return;
    }

    added = cfg->token_rate_bytes_per_ms * (float)elapsed_ms;
    burst = (float)cfg->token_burst_bytes;
    st->tokens += added;
    if (st->tokens > burst) {
        st->tokens = burst;
    }
    st->last_token_ms = now_ms;
}

/**
 * @brief 周期性步进：超时检测、报警判断、更新遥测并发布
 *
 * 若自发送起超过 latency_alarm_ms 未收到 on_rx，则视为超时并清除 prev_tx_ms。
 * 遥测状态字 status 含 BM_TRANSPORT_QOS_TEL_VALID 及可选 BM_TRANSPORT_QOS_TEL_ALARM。
 * 每步按经过时间补充令牌桶。
 *
 * @param axis QoS 轴实例（NULL 时直接返回）
 */
void bm_transport_qos_step(bm_transport_qos_axis_t *axis) {
    const bm_transport_qos_config_t *cfg;
    bm_transport_qos_state_t *st;
    uint32_t status;
    uint32_t now_ms;

    if (axis == NULL) {
        return;
    }

    cfg = &axis->config;
    st = &axis->state;

    token_refill(axis);

    if (st->prev_tx_ms != 0u && axis->resources.now_ms != NULL) {
        now_ms = axis->resources.now_ms(axis->resources.now_ms_user);
        if ((uint32_t)((int32_t)(now_ms - st->prev_tx_ms)) >
            cfg->latency_alarm_ms) {
            st->prev_tx_ms = 0u;
        }
    }

    status = BM_TRANSPORT_QOS_TEL_VALID;
    if (st->latency_ema_ms > cfg->latency_alarm_ms ||
        st->jitter_ms > cfg->jitter_alarm_ms) {
        status |= BM_TRANSPORT_QOS_TEL_ALARM;
    }

    st->step_count++;
    st->telemetry.sequence = st->step_count;
    st->telemetry.status = status;
    st->telemetry.latency_ms = st->latency_ms;
    st->telemetry.jitter_ms = st->jitter_ms;
    st->telemetry.latency_ema_ms = st->latency_ema_ms;

    if (axis->resources.publish_telemetry != NULL) {
        axis->resources.publish_telemetry(
            axis->resources.publish_telemetry_user, &st->telemetry);
    }
}

/**
 * @brief token bucket 入队整形（消耗令牌）
 *
 * 先按经过时间补充令牌，若令牌充足则扣除 bytes 令牌后返回 0（接受）；
 * 若 token_rate/burst 未配置则直接放行（返回 0）。
 *
 * @param axis  QoS 轴实例（不可为 NULL）
 * @param bytes 待发送字节数（不可为 0）
 * @return 0 接受；-1 丢弃（桶空或参数无效）
 */
int bm_transport_qos_enqueue(bm_transport_qos_axis_t *axis, uint32_t bytes) {
    const bm_transport_qos_config_t *cfg;

    if (axis == NULL || bytes == 0u) {
        return -1;
    }

    cfg = &axis->config;
    if (cfg->token_rate_bytes_per_ms <= 0.0f ||
        cfg->token_burst_bytes == 0u) {
        return 0;
    }

    token_refill(axis);

    if (axis->state.tokens < (float)bytes) {
        return -1;
    }

    axis->state.tokens -= (float)bytes;
    return 0;
}
