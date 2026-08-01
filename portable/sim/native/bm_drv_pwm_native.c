/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_pwm_native.c
 * @brief native_sim PWM 设备驱动
 * @maturity E1
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-31
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-07-31       1.1            zeh            fire_update 回调派发首尾成对调用
 *                                                bm_hrt_isr_enter/exit，与真实 Hardware
 *                                                HRT 端口一致，消除"仿真放行、真机拒绝"分叉
 *
 *
 * @par ????:
 * 2026-08-01       1.1            Codex            补全中文 Doxygen 合规注释
 */
#include "bm_hal_pwm_sim.h"
#include "bm/common/bm_critical_wrap.h"
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

/**
 * @brief 获取 PWM 设备绑定的仿真状态。
 * @param pwm PWM 设备实例。
 * @return 有效时返回设备状态指针；设备或配置无效时返回 NULL。
 */
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

/**
 * @brief 设置 PWM 指定相的仿真占空比值。
 * @param pwm PWM 设备实例。
 * @param phase PWM 相索引。
 * @param duty PWM 占空比编码值，取值语义与 HAL 接口一致。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int native_pwm_set_duty(const struct bm_hal_pwm *pwm, uint32_t phase, uint16_t duty) {
    pwm_sim_state_t *state = pwm_state_for(pwm);
    if (!state || phase >= BM_SIM_PWM_MAX_PHASES) {
        BM_LOGE(TAG, "set_duty: invalid pwm=%p phase=%u", (const void *)pwm, phase);
        return BM_ERR_INVALID;
    }
    state->duty[phase] = duty;
    return BM_OK;
}

/**
 * @brief 启用或禁用 PWM 仿真输出。
 * @param pwm PWM 设备实例。
 * @param enable 非 0 表示启用，0 表示禁用。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
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

/**
 * @brief 将 PWM 仿真输出切换到安全状态。
 * @param pwm PWM 设备实例。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
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

/**
 * @brief 绑定 PWM 更新 HRT 回调。
 * @param pwm PWM 设备实例。
 * @param binding HRT 回调绑定信息；传入 NULL 时解除绑定。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
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
    /* 与真实 Hardware HRT 端口一致（bm_critical_wrap.h 契约）：
     * 派发等价于硬件 IRQ 的回调须标记 HRT ISR 上下文，
     * 避免"仿真放行、真机拒绝"分叉 */
    bm_hrt_isr_enter();
    state->update_binding.callback(state->update_binding.context);
    bm_hrt_isr_exit();
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
