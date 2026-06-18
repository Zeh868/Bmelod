/**
 * @file bm_hal_pwm.c
 * @brief PWM HAL 分发层（契约 → driver API）
 *
 * 将占空比、输出使能等操作转发至已绑定的 driver API。
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
#include "bm_hal_pwm.h"
#include "bm_types.h"

int bm_hal_pwm_set_duty(const bm_hal_pwm_t *pwm, uint32_t phase, uint16_t duty) {
    if (!pwm || !pwm->api || !pwm->api->set_duty) {
        return BM_ERR_NOT_INIT;
    }
    return pwm->api->set_duty(pwm, phase, duty);
}

int bm_hal_pwm_enable_outputs(const bm_hal_pwm_t *pwm, int enable) {
    if (!pwm || !pwm->api || !pwm->api->enable_outputs) {
        return BM_ERR_NOT_INIT;
    }
    return pwm->api->enable_outputs(pwm, enable);
}

int bm_hal_pwm_request_safe_state(const bm_hal_pwm_t *pwm) {
    if (!pwm || !pwm->api || !pwm->api->request_safe_state) {
        return BM_ERR_NOT_INIT;
    }
    return pwm->api->request_safe_state(pwm);
}

int bm_hal_pwm_bind_update(const bm_hal_pwm_t *pwm,
                           const bm_hal_hrt_binding_t *binding) {
    if (!pwm || !pwm->api || !pwm->api->bind_update) {
        return BM_ERR_NOT_INIT;
    }
    if (!binding) {
        return BM_ERR_INVALID;
    }
    return pwm->api->bind_update(pwm, binding);
}
