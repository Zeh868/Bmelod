/**
 * @file transport_qos.c
 * @brief 传输延迟与抖动监控实现
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 *
 */
#include "bm/component/transport_qos.h"
#include "bm/algorithm/bm_algo_common.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

int bm_transport_qos_validate_config(const bm_transport_qos_config_t *config) {
    if (config == NULL || config->ema_alpha <= 0.0f ||
        config->ema_alpha > 1.0f) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

void bm_transport_qos_reset(bm_transport_qos_axis_t *axis) {
    if (axis == NULL) {
        return;
    }

    axis->state.prev_tx_ms = 0u;
    axis->state.latency_ms = 0.0f;
    axis->state.jitter_ms = 0.0f;
    axis->state.latency_ema_ms = 0.0f;
    axis->state.tokens = 0.0f;
    axis->state.have_prev = 0;
    axis->state.step_count = 0u;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
}

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

void bm_transport_qos_on_tx(bm_transport_qos_axis_t *axis) {
    if (axis == NULL || axis->resources.now_ms == NULL) {
        return;
    }
    axis->state.prev_tx_ms =
        axis->resources.now_ms(axis->resources.now_ms_user);
}

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

    if (axis->state.tokens < (float)bytes) {
        return -1;
    }

    axis->state.tokens -= (float)bytes;
    return 0;
}
