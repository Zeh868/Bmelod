/**
 * @file bm_drv_pwm_native.c
 * @brief native_sim PWM 设备驱动
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 *
 */
#include "bm_hal_pwm_sim.h"
#include "bm_log.h"

#include <string.h>

#define BM_SIM_PWM_MAX_PHASES 3u
#define BM_SIM_PWM_INSTANCES  4u
#define TAG                   "hal_pwm"

typedef struct {
    uint32_t id;
} bm_pwm_native_config_t;

typedef struct {
    uint16_t duty[BM_SIM_PWM_MAX_PHASES];
    int outputs_enabled;
    bm_hal_hrt_binding_t update_binding;
} pwm_sim_state_t;

static pwm_sim_state_t g_pwm_state[BM_SIM_PWM_INSTANCES];

static pwm_sim_state_t *pwm_state_for(const bm_hal_pwm_t *pwm) {
    const bm_pwm_native_config_t *cfg;

    if (!pwm || !pwm->config) {
        return NULL;
    }
    cfg = (const bm_pwm_native_config_t *)pwm->config;
    if (cfg->id >= BM_SIM_PWM_INSTANCES) {
        return NULL;
    }
    return &g_pwm_state[cfg->id];
}

static int native_pwm_set_duty(const struct bm_hal_pwm *pwm, uint32_t phase, uint16_t duty) {
    pwm_sim_state_t *state = pwm_state_for(pwm);
    if (!state || phase >= BM_SIM_PWM_MAX_PHASES) {
        BM_LOGE(TAG, "set_duty: invalid pwm=%p phase=%u", (const void *)pwm, phase);
        return BM_ERR_INVALID;
    }
    state->duty[phase] = duty;
    return BM_OK;
}

static int native_pwm_enable_outputs(const struct bm_hal_pwm *pwm, int enable) {
    const bm_pwm_native_config_t *cfg;
    pwm_sim_state_t *state = pwm_state_for(pwm);

    if (!state) {
        BM_LOGE(TAG, "enable_outputs: invalid pwm=%p", (const void *)pwm);
        return BM_ERR_INVALID;
    }
    cfg = (const bm_pwm_native_config_t *)pwm->config;
    state->outputs_enabled = enable ? 1 : 0;
    BM_LOGI(TAG, "enable_outputs: pwm id=%u enable=%d", cfg->id, enable);
    return BM_OK;
}

static int native_pwm_request_safe_state(const struct bm_hal_pwm *pwm) {
    const bm_pwm_native_config_t *cfg;
    pwm_sim_state_t *state = pwm_state_for(pwm);

    if (!state) {
        BM_LOGW(TAG, "request_safe_state: invalid pwm=%p", (const void *)pwm);
        return BM_ERR_INVALID;
    }
    cfg = (const bm_pwm_native_config_t *)pwm->config;
    state->outputs_enabled = 0;
    memset(state->duty, 0, sizeof(state->duty));
    BM_LOGI(TAG, "request_safe_state: pwm id=%u", cfg->id);
    return BM_OK;
}

static int native_pwm_bind_update(const struct bm_hal_pwm *pwm,
                                  const bm_hal_hrt_binding_t *binding) {
    const bm_pwm_native_config_t *cfg;
    pwm_sim_state_t *state = pwm_state_for(pwm);

    if (!state) {
        BM_LOGE(TAG, "bind_update: invalid pwm=%p", (const void *)pwm);
        return BM_ERR_INVALID;
    }
    cfg = (const bm_pwm_native_config_t *)pwm->config;
    if (!binding) {
        memset(&state->update_binding, 0, sizeof(state->update_binding));
        BM_LOGI(TAG, "bind_update: unbound pwm id=%u", cfg->id);
        return BM_OK;
    }
    state->update_binding = *binding;
    BM_LOGI(TAG, "bind_update: bound pwm id=%u", cfg->id);
    return BM_OK;
}

static const struct bm_pwm_driver_api bm_pwm_native_api = {
    native_pwm_set_duty,
    native_pwm_enable_outputs,
    native_pwm_request_safe_state,
    native_pwm_bind_update,
};

static const bm_pwm_native_config_t bm_pwm_cfg0 = { 0u };
static const bm_pwm_native_config_t bm_pwm_cfg1 = { 1u };
static const bm_pwm_native_config_t bm_pwm_cfg2 = { 2u };

const bm_hal_pwm_t BM_HAL_PWM_SIM0 = { &bm_pwm_native_api, &bm_pwm_cfg0 };
const bm_hal_pwm_t BM_HAL_PWM_SIM1 = { &bm_pwm_native_api, &bm_pwm_cfg1 };
const bm_hal_pwm_t BM_HAL_PWM_SIM2 = { &bm_pwm_native_api, &bm_pwm_cfg2 };

void bm_hal_pwm_sim_fire_update(const bm_hal_pwm_t *pwm) {
    pwm_sim_state_t *state = pwm_state_for(pwm);
    if (!state || !state->update_binding.callback) {
        return;
    }
    state->update_binding.callback(state->update_binding.context);
}

uint16_t bm_hal_pwm_sim_get_duty(const bm_hal_pwm_t *pwm, uint32_t phase) {
    pwm_sim_state_t *state = pwm_state_for(pwm);
    if (!state || phase >= BM_SIM_PWM_MAX_PHASES) {
        return 0u;
    }
    return state->duty[phase];
}

int bm_hal_pwm_sim_outputs_enabled(const bm_hal_pwm_t *pwm) {
    pwm_sim_state_t *state = pwm_state_for(pwm);
    return state ? state->outputs_enabled : 0;
}
