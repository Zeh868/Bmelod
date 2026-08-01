/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_comp_native.c
 * @brief native_sim 比较器设备驱动
 * @maturity E1
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-08-01       1.0            zeh            补全中文 Doxygen 合规注释
 */
#include "bm_hal_comp_sim.h"
#include "bm_log.h"

#define TAG "hal_comp"

typedef struct {
    uint32_t id;
} bm_comp_native_config_t;

typedef struct {
    uint16_t threshold;
    int latched;
} comp_sim_state_t;

static comp_sim_state_t g_comp_state[2];

/**
 * @brief 获取比较器设备绑定的仿真状态。
 * @param comp 比较器设备实例。
 * @return 有效时返回设备状态指针；设备或配置无效时返回 NULL。
 */
static comp_sim_state_t *comp_state_for(const bm_hal_comp_t *comp) {
    const bm_comp_native_config_t *cfg;

    if (!comp || !comp->config) {
        return NULL;
    }
    cfg = (const bm_comp_native_config_t *)comp->config;
    if (cfg->id >= 2u) {
        return NULL;
    }
    return &g_comp_state[cfg->id];
}

/**
 * @brief 清除比较器仿真触发锁存。
 * @param comp 比较器设备实例。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int native_comp_clear_latch(const struct bm_hal_comp *comp) {
    comp_sim_state_t *state = comp_state_for(comp);
    if (!state) {
        BM_LOGE(TAG, "clear_latch: invalid comp=%p", (const void *)comp);
        return BM_ERR_INVALID;
    }
    state->latched = 0;
    return BM_OK;
}

static const struct bm_comp_driver_api bm_comp_native_api = {
    native_comp_clear_latch,
};

static const bm_comp_native_config_t bm_comp_cfg0 = { 0u };

const bm_hal_comp_t BM_HAL_COMP_SIM0 = { &bm_comp_native_api, &bm_comp_cfg0 };

void bm_hal_comp_sim_set_threshold(const bm_hal_comp_t *comp, uint16_t threshold) {
    comp_sim_state_t *state = comp_state_for(comp);
    if (!state) {
        BM_LOGW(TAG, "set_threshold: invalid comp=%p", (const void *)comp);
        return;
    }
    state->threshold = threshold;
}

void bm_hal_comp_sim_trip(const bm_hal_comp_t *comp, uint16_t sample) {
    comp_sim_state_t *state = comp_state_for(comp);
    if (!state) {
        BM_LOGW(TAG, "trip: invalid comp=%p", (const void *)comp);
        return;
    }
    if (sample >= state->threshold) {
        state->latched = 1;
    }
}

int bm_hal_comp_sim_is_latched(const bm_hal_comp_t *comp) {
    comp_sim_state_t *state = comp_state_for(comp);
    if (!state) {
        return 0;
    }
    return state->latched;
}
