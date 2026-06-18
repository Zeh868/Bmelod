/**
 * @file bm_drv_adc_native.c
 * @brief native_sim ADC 设备驱动
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
#include "bm_hal_adc_sim.h"
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
    state->complete_binding.callback(state->complete_binding.context);
}
