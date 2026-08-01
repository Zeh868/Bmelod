/**
 * @file process_sequence.c
 * @brief 简化 IEC 定时器与顺序状态机实现
 *
 * 维护 TON/TOF 计时与顺序步索引。推进规则：联锁满足且步内计时达到该步的
 * 驻留时间（steps[].timeout_s，实为最短保持时间而非超时门限）后进入下一步；
 * 联锁不满足时仅阻塞在当前步等待，本状态机不实现故障态，也无超时转故障逻辑。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.3
 * @date 2026-08-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            补 SPDX 与函数级 Doxygen
 * 2026-08-01       0.3            zeh          新增兼容的 exec 生命周期适配
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/component/process_sequence.h"
#include "bm/algorithm/bm_algo_common.h"
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
    if (config == NULL ||
        !bm_algo_is_finite_f(config->dt_s) || config->dt_s <= 0.0f ||
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

/**
 * @brief exec 生命周期初始化：校验配置并复位顺序状态机
 * @param instance exec 实例
 * @return BM_OK 成功；BM_ERR_INVALID 指针或配置非法
 */
int bm_process_sequence_exec_init(const bm_exec_t *instance) {
    bm_process_sequence_exec_context_t *context;

    if (instance == NULL || instance->state == NULL) {
        return BM_ERR_INVALID;
    }
    context = (bm_process_sequence_exec_context_t *)instance->state;
    if (context->axis == NULL ||
        bm_process_sequence_validate_config(&context->axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }

    bm_process_sequence_reset(context->axis);
    return BM_OK;
}

/**
 * @brief exec 生命周期启动：启动顺序状态机
 * @param instance exec 实例
 * @return BM_OK 成功；BM_ERR_INVALID 指针非法
 */
int bm_process_sequence_exec_start(const bm_exec_t *instance) {
    bm_process_sequence_exec_context_t *context;

    if (instance == NULL || instance->state == NULL) {
        return BM_ERR_INVALID;
    }
    context = (bm_process_sequence_exec_context_t *)instance->state;
    if (context->axis == NULL) {
        return BM_ERR_INVALID;
    }

    bm_process_sequence_start(context->axis);
    return BM_OK;
}

/**
 * @brief exec 周期运行：使用适配资源推进顺序状态机
 * @param instance exec 实例；无效时静默返回
 */
void bm_process_sequence_exec_run(const bm_exec_t *instance) {
    bm_process_sequence_exec_context_t *context;

    if (instance == NULL || instance->state == NULL) {
        return;
    }
    context = (bm_process_sequence_exec_context_t *)instance->state;
    if (context->axis == NULL) {
        return;
    }

    bm_process_sequence_step(context->axis,
                             context->resources.interlock,
                             context->resources.interlock_user);
}

/**
 * @brief exec 安全停止：复位顺序状态机
 * @param instance exec 实例；无效时静默返回
 */
void bm_process_sequence_exec_safe_stop(const bm_exec_t *instance) {
    bm_process_sequence_exec_context_t *context;

    if (instance == NULL || instance->state == NULL) {
        return;
    }
    context = (bm_process_sequence_exec_context_t *)instance->state;
    if (context->axis == NULL) {
        return;
    }

    bm_process_sequence_reset(context->axis);
}

/** @brief process_sequence 标准 exec 生命周期操作表 */
const bm_exec_ops_t bm_process_sequence_exec_ops = {
    bm_process_sequence_exec_init,
    bm_process_sequence_exec_start,
    bm_process_sequence_exec_safe_stop
};
