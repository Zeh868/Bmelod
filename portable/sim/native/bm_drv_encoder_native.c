/**
 * @file bm_drv_encoder_native.c
 * @brief native_sim 编码器设备驱动
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
#include "bm_hal_encoder.h"
#include "bm_log.h"

#define TAG "hal_encoder"

typedef struct {
    uint32_t id;
} bm_encoder_native_config_t;

static int32_t g_encoder_count[2];
static int     g_encoder_fail[2];

static const bm_encoder_native_config_t *encoder_cfg(const bm_hal_encoder_t *enc) {
    if (!enc || !enc->config) {
        return NULL;
    }
    return (const bm_encoder_native_config_t *)enc->config;
}

static int native_encoder_read(const struct bm_hal_encoder *enc, int32_t *value) {
    const bm_encoder_native_config_t *cfg = encoder_cfg(enc);
    if (!cfg || !value || cfg->id >= 2u) {
        BM_LOGE(TAG, "read: invalid enc=%p value=%p", (const void *)enc, (const void *)value);
        return BM_ERR_INVALID;
    }
    if (g_encoder_fail[cfg->id]) {
        return BM_ERR_INVALID;  /* 测试注入：模拟读失败 */
    }
    *value = g_encoder_count[cfg->id];
    return BM_OK;
}

static const struct bm_encoder_driver_api bm_encoder_native_api = {
    native_encoder_read,
};

static const bm_encoder_native_config_t bm_encoder_cfg0 = { 0u };

const bm_hal_encoder_t BM_HAL_ENC_SIM0 = { &bm_encoder_native_api, &bm_encoder_cfg0 };

void bm_hal_encoder_sim_set_count(const bm_hal_encoder_t *enc, int32_t value) {
    const bm_encoder_native_config_t *cfg = encoder_cfg(enc);
    if (!cfg || cfg->id >= 2u) {
        BM_LOGW(TAG, "set_count: invalid enc=%p", (const void *)enc);
        return;
    }
    g_encoder_count[cfg->id] = value;
}

void bm_hal_encoder_sim_set_fail(const bm_hal_encoder_t *enc, int fail) {
    const bm_encoder_native_config_t *cfg = encoder_cfg(enc);
    if (!cfg || cfg->id >= 2u) {
        BM_LOGW(TAG, "set_fail: invalid enc=%p", (const void *)enc);
        return;
    }
    g_encoder_fail[cfg->id] = fail;
}
