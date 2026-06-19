/**
 * @file bm_vendor_bmi160_esp32_idf.c
 * @brief ESP32-WROOM-32E vendor 专用 BMI160 接口占位实现
 *
 * 由于当前仓库尚未确认 BMI160 的实际板级连线与总线拓扑，
 * 本文件保留 API 但明确返回“不支持”，避免保留错误的驱动路径。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            新增 BMI160 vendor 入口
 * 2026-06-19       1.1            Codex          删除总线假实现，仅保留明确返回
 *
 */
#include "bm_vendor_bmi160_esp32_idf.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**
 * @brief 规范化配置。
 */
static void bm_vendor_bmi160_normalize_config(const bm_vendor_bmi160_config_t *cfg,
                                              bm_vendor_bmi160_config_t *out) {
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (cfg == NULL) {
        return;
    }
    *out = *cfg;
    if (out->address == 0u) {
        out->address = BM_VENDOR_BMI160_I2C_ADDR_DEFAULT;
    }
    if (out->clock_hz == 0u) {
        out->clock_hz = (out->bus_type == BM_VENDOR_BMI160_BUS_SPI) ? 1000000u : 400000u;
    }
    if (out->acc_conf == 0u) {
        out->acc_conf = BM_VENDOR_BMI160_DEFAULT_ACC_CONF;
    }
    if (out->acc_range == 0u) {
        out->acc_range = BM_VENDOR_BMI160_DEFAULT_ACC_RANGE;
    }
    if (out->gyr_conf == 0u) {
        out->gyr_conf = BM_VENDOR_BMI160_DEFAULT_GYR_CONF;
    }
    if (out->gyr_range == 0u) {
        out->gyr_range = BM_VENDOR_BMI160_DEFAULT_GYR_RANGE;
    }
}

/**
 * @brief BMI160 运行时句柄。
 */
struct bm_vendor_bmi160_handle {
    /** @brief 规范化后的配置。 */
    bm_vendor_bmi160_config_t config;
    /** @brief 是否已标记为可用。 */
    int initialized;
};

/**
 * @brief 初始化 BMI160 句柄。
 */
int bm_vendor_bmi160_init(bm_vendor_bmi160_handle_t *handle,
                          const bm_vendor_bmi160_config_t *config) {
    if (handle == NULL || config == NULL) {
        return BM_ERR_INVALID;
    }
    memset(handle, 0, sizeof(*handle));
    bm_vendor_bmi160_normalize_config(config, &handle->config);
    handle->initialized = 0;
    return BM_ERR_NOT_SUPPORTED;
}

/**
 * @brief 反初始化 BMI160。
 */
int bm_vendor_bmi160_deinit(bm_vendor_bmi160_handle_t *handle) {
    if (handle == NULL) {
        return BM_ERR_INVALID;
    }
    handle->initialized = 0;
    return BM_OK;
}

/**
 * @brief 读取 BMI160 原始数据。
 */
int bm_vendor_bmi160_read_raw(const bm_vendor_bmi160_handle_t *handle,
                              bm_vendor_bmi160_sample_t *sample) {
    if (handle == NULL || sample == NULL) {
        return BM_ERR_INVALID;
    }
    memset(sample, 0, sizeof(*sample));
    (void)handle;
    return BM_ERR_NOT_SUPPORTED;
}

/**
 * @brief 获取当前配置视图。
 */
const bm_vendor_bmi160_config_t *bm_vendor_bmi160_get_config(
    const bm_vendor_bmi160_handle_t *handle) {
    if (handle == NULL || handle->initialized == 0) {
        return NULL;
    }
    return &handle->config;
}

