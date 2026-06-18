/**
 * @file additive_motion.c
 * @brief Z 轴 ZV 输入整形实现
 *
 * 两脉冲零振动整形器，半周期延迟第二脉冲。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            ZV 两脉冲骨架
 * 2026-06-17       0.2            zeh            pressure advance 线性模型
 */
#include "bm/component/additive_motion.h"
#include "bm/algorithm/bm_algo_common.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

static void zv_compute_coeffs(bm_additive_motion_axis_t *axis) {
    const bm_additive_motion_config_t *cfg = &axis->config;
    bm_additive_motion_state_t *st = &axis->state;
    float wn;
    float zeta;
    float K;
    float td;
    uint32_t steps;

    wn = 2.0f * 3.14159265f * cfg->natural_freq_hz;
    zeta = (cfg->damping_ratio > 0.0f) ? cfg->damping_ratio : 0.01f;
    K = expf(-zeta * 3.14159265f / sqrtf(1.0f - zeta * zeta));
    st->a0 = 1.0f / (1.0f + K);
    st->a1 = K / (1.0f + K);
    td = 3.14159265f / wn;
    st->delay_s = td;
    steps = (cfg->dt_s > 0.0f) ? (uint32_t)(td / cfg->dt_s + 0.5f) : 1u;
    if (steps == 0u) {
        steps = 1u;
    }
    if (steps >= BM_ADDITIVE_ZV_BUFFER_MAX) {
        steps = BM_ADDITIVE_ZV_BUFFER_MAX - 1u;
    }
    st->delay_steps = steps;
    st->buffer_len = steps + 1u;
    st->buffer_head = 0u;
}

int bm_additive_motion_validate_config(const bm_additive_motion_config_t *config) {
    if (config == NULL || config->dt_s <= 0.0f ||
        config->natural_freq_hz <= 0.0f ||
        config->max_velocity_mm_s <= 0.0f) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

void bm_additive_motion_reset(bm_additive_motion_axis_t *axis) {
    if (axis == NULL) {
        return;
    }
    memset(axis->state.buffer, 0, sizeof(axis->state.buffer));
    axis->state.buffer_head = 0u;
    axis->state.last_cmd_mm = 0.0f;
    axis->state.shaped_mm = 0.0f;
    axis->state.step_count = 0u;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
    zv_compute_coeffs(axis);
}

int bm_additive_motion_init(bm_additive_motion_axis_t *axis) {
    if (axis == NULL ||
        bm_additive_motion_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_additive_motion_reset(axis);
    return BM_OK;
}

void bm_additive_motion_shape_cmd(bm_additive_motion_axis_t *axis, float cmd_mm) {
    bm_additive_motion_state_t *st;
    float delta;
    float out;
    uint32_t delayed_idx;
    float delayed;

    if (axis == NULL) {
        return;
    }

    st = &axis->state;
    delta = cmd_mm - st->last_cmd_mm;
    st->last_cmd_mm = cmd_mm;

    delayed_idx = (st->buffer_head + st->buffer_len - st->delay_steps) %
                  st->buffer_len;
    delayed = st->buffer[delayed_idx];

    out = st->a0 * delta + st->a1 * delayed;
    st->shaped_mm += out;

    st->buffer[st->buffer_head] = delta;
    st->buffer_head = (st->buffer_head + 1u) % st->buffer_len;

    st->telemetry.raw_cmd_mm = cmd_mm;
    st->telemetry.shaped_cmd_mm = st->shaped_mm;
}

void bm_additive_motion_step(bm_additive_motion_axis_t *axis) {
    const bm_additive_motion_config_t *cfg;
    bm_additive_motion_state_t *st;
    float pos = 0.0f;
    float vel;
    float prev;

    if (axis == NULL ||
        bm_additive_motion_validate_config(&axis->config) != BM_OK) {
        return;
    }

    cfg = &axis->config;
    st = &axis->state;
    prev = st->shaped_mm;

    if (axis->resources.read_z != NULL) {
        (void)axis->resources.read_z(axis->resources.read_z_user, &pos);
    }

    vel = (st->shaped_mm - prev) / cfg->dt_s;
    if (fabsf(vel) > cfg->max_velocity_mm_s) {
        vel = (vel > 0.0f) ? cfg->max_velocity_mm_s : -cfg->max_velocity_mm_s;
    }

    if (axis->resources.write_z != NULL) {
        (void)axis->resources.write_z(axis->resources.write_z_user,
                                      st->shaped_mm);
    }

    st->step_count++;
    st->telemetry.sequence = st->step_count;
    st->telemetry.velocity_mm_s = vel;

    if (axis->resources.publish_telemetry != NULL) {
        axis->resources.publish_telemetry(
            axis->resources.publish_telemetry_user, &st->telemetry);
    }
}

float bm_additive_motion_pressure_advance(float velocity_mm_s, float factor) {
    return velocity_mm_s * factor;
}
