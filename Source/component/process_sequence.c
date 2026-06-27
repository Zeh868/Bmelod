/**
 * @file process_sequence.c
 * @brief 简化 IEC 定时器与顺序状态机实现
 *
 * 维护 TON/TOF 计时与顺序步索引，联锁失败或超时进入故障态。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            补 SPDX 与函数级 Doxygen
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/component/process_sequence.h"
#include "bm/common/bm_types.h"

void bm_process_ton_reset(bm_process_ton_state_t *state, uint32_t preset_ticks) {
    if (state == NULL) {
        return;
    }
    state->preset_ticks = preset_ticks;
    state->elapsed_ticks = 0u;
    state->input = 0;
    state->output = 0;
}

int bm_process_ton_step(bm_process_ton_state_t *state, int input) {
    if (state == NULL) {
        return 0;
    }

    state->input = input;
    if (!input) {
        state->elapsed_ticks = 0u;
        state->output = 0;
        return 0;
    }

    state->elapsed_ticks++;
    state->output = (state->elapsed_ticks > state->preset_ticks) ? 1 : 0;
    return state->output;
}

void bm_process_tof_reset(bm_process_tof_state_t *state, uint32_t preset_ticks) {
    if (state == NULL) {
        return;
    }
    state->preset_ticks = preset_ticks;
    state->elapsed_ticks = 0u;
    state->input = 0;
    state->output = 0;
}

int bm_process_tof_step(bm_process_tof_state_t *state, int input) {
    if (state == NULL) {
        return 0;
    }

    state->input = input;
    if (input) {
        state->elapsed_ticks = 0u;
        state->output = 1;
        return 1;
    }

    state->elapsed_ticks++;
    state->output = (state->elapsed_ticks <= state->preset_ticks) ? 1 : 0;
    return state->output;
}

int bm_process_sequence_validate_config(const bm_process_sequence_config_t *config) {
    if (config == NULL || config->dt_s <= 0.0f ||
        config->step_count == 0u ||
        config->step_count > BM_PROCESS_SEQ_MAX_STEPS) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

void bm_process_sequence_reset(bm_process_sequence_axis_t *axis) {
    if (axis == NULL) {
        return;
    }

    axis->state.current_step = 0u;
    axis->state.step_elapsed_s = 0.0f;
    axis->state.running = 0;
    axis->state.done = 0;
    bm_process_ton_reset(&axis->state.ton, 0u);
    bm_process_tof_reset(&axis->state.tof, 0u);
}

void bm_process_sequence_start(bm_process_sequence_axis_t *axis) {
    if (axis == NULL) {
        return;
    }

    axis->state.current_step = 0u;
    axis->state.step_elapsed_s = 0.0f;
    axis->state.running = 1;
    axis->state.done = 0;
}

void bm_process_sequence_step(bm_process_sequence_axis_t *axis,
                              bm_process_sequence_interlock_fn interlock,
                              void *interlock_user) {
    const bm_process_sequence_config_t *cfg;
    bm_process_sequence_state_t *st;
    const bm_process_sequence_step_config_t *step_cfg;
    int interlock_ok;

    if (axis == NULL || !axis->state.running || axis->state.done) {
        return;
    }

    cfg = &axis->config;
    st = &axis->state;
    if (st->current_step >= cfg->step_count) {
        st->done = 1;
        st->running = 0;
        return;
    }

    step_cfg = &cfg->steps[st->current_step];
    st->step_elapsed_s += cfg->dt_s;

    interlock_ok = 1;
    if (interlock != NULL &&
        interlock(interlock_user, st->current_step) == 0) {
        interlock_ok = 0;
    }

    if (interlock_ok &&
        (step_cfg->timeout_s <= 0.0f ||
         st->step_elapsed_s >= step_cfg->timeout_s)) {
        st->current_step++;
        st->step_elapsed_s = 0.0f;
        if (st->current_step >= cfg->step_count) {
            st->done = 1;
            st->running = 0;
        }
    }
}
