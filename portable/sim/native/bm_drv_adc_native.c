/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_adc_native.c
 * @brief native_sim ADC 设备驱动
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
 * 2026-07-31       1.1            zeh            fire_complete 回调派发首尾成对调用
 *                                                bm_hrt_isr_enter/exit，与真实 Hardware
 *                                                HRT 端口一致，消除"仿真放行、真机拒绝"分叉
 * 2026-08-01       1.1            zeh            补全中文 Doxygen 合规注释
 */
#include "bm_hal_adc_sim.h"
#include "bm/common/bm_critical_wrap.h"
#include "bm_log.h"

#define BM_SIM_ADC_RANKS      16u
#define BM_SIM_ADC_INSTANCES  2u
#define TAG                   "hal_adc"

typedef struct {
    uint32_t id;
} bm_adc_native_config_t;

typedef struct {
    uint16_t waveform[BM_SIM_ADC_RANKS];
    bm_hal_hrt_binding_t complete_binding;
} adc_sim_state_t;

static adc_sim_state_t g_adc_state[BM_SIM_ADC_INSTANCES];

/**
 * @brief 获取 ADC 设备绑定的仿真状态。
 * @param adc ADC 设备实例。
 * @return 有效时返回设备状态指针；设备或配置无效时返回 NULL。
 */
static adc_sim_state_t *adc_state_for(const bm_hal_adc_t *adc) {
    const bm_adc_native_config_t *cfg;

    if (!adc || !adc->config) {
        return NULL;
    }
    cfg = (const bm_adc_native_config_t *)adc->config;
    if (cfg->id >= BM_SIM_ADC_INSTANCES) {
        return NULL;
    }
    return &g_adc_state[cfg->id];
}

/**
 * @brief 读取 ADC 注入序列的仿真采样值。
 * @param adc ADC 设备实例。
 * @param rank ADC 注入序列排名。
 * @param value 用于接收读取值的输出指针；不得为 NULL。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int native_adc_read_injected(const struct bm_hal_adc *adc,
                                    uint32_t rank, uint16_t *value) {
    adc_sim_state_t *state = adc_state_for(adc);
    if (!state || !value || rank >= BM_SIM_ADC_RANKS) {
        BM_LOGE(TAG, "read_injected: invalid adc=%p value=%p rank=%u",
                (const void *)adc, (const void *)value, rank);
        return BM_ERR_INVALID;
    }
    *value = state->waveform[rank];
    return BM_OK;
}

/**
 * @brief 绑定 ADC 转换完成的 HRT 回调。
 * @param adc ADC 设备实例。
 * @param binding HRT 回调绑定信息；传入 NULL 时解除绑定。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int native_adc_bind_complete(const struct bm_hal_adc *adc,
                                    const bm_hal_hrt_binding_t *binding) {
    const bm_adc_native_config_t *cfg;
    adc_sim_state_t *state = adc_state_for(adc);

    if (!state) {
        BM_LOGE(TAG, "bind_complete: invalid adc=%p", (const void *)adc);
        return BM_ERR_INVALID;
    }
    cfg = (const bm_adc_native_config_t *)adc->config;
    if (!binding) {
        state->complete_binding.callback = NULL;
        state->complete_binding.context = NULL;
        BM_LOGI(TAG, "bind_complete: unbound adc id=%u", cfg->id);
        return BM_OK;
    }
    state->complete_binding = *binding;
    BM_LOGI(TAG, "bind_complete: bound adc id=%u", cfg->id);
    return BM_OK;
}

static const struct bm_adc_driver_api bm_adc_native_api = {
    native_adc_read_injected,
    native_adc_bind_complete,
};

static const bm_adc_native_config_t bm_adc_cfg0 = { 0u };
static const bm_adc_native_config_t bm_adc_cfg1 = { 1u };

const bm_hal_adc_t BM_HAL_ADC_SIM0 = { &bm_adc_native_api, &bm_adc_cfg0 };
const bm_hal_adc_t BM_HAL_ADC_SIM1 = { &bm_adc_native_api, &bm_adc_cfg1 };

void bm_hal_adc_sim_set_rank(const bm_hal_adc_t *adc, uint32_t rank,
                             uint16_t value) {
    adc_sim_state_t *state = adc_state_for(adc);
    if (!state || rank >= BM_SIM_ADC_RANKS) {
        BM_LOGW(TAG, "set_rank: invalid adc=%p rank=%u", (const void *)adc, rank);
        return;
    }
    state->waveform[rank] = value;
}

void bm_hal_adc_sim_fire_complete(const bm_hal_adc_t *adc) {
    adc_sim_state_t *state = adc_state_for(adc);
    if (!state || !state->complete_binding.callback) {
        return;
    }
    /* 与真实 Hardware HRT 端口一致（bm_critical_wrap.h 契约）：
     * 派发等价于硬件 IRQ 的回调须标记 HRT ISR 上下文，
     * 避免"仿真放行、真机拒绝"分叉 */
    bm_hrt_isr_enter();
    state->complete_binding.callback(state->complete_binding.context);
    bm_hrt_isr_exit();
}
