/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_adc.c
 * @brief ADC HAL 分发层（契约 → driver API）
 *
 * 将注入通道读取与 HRT 绑定等操作转发至已绑定的 driver API。
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
#include "bm_hal_adc.h"
#include "bm_types.h"

int bm_hal_adc_read_injected(const bm_hal_adc_t *adc,
                             uint32_t rank, uint16_t *value) {
    if (!adc || !adc->api || !adc->api->read_injected) {
        return BM_ERR_NOT_INIT;
    }
    if (!value) {
        return BM_ERR_INVALID;
    }
    return adc->api->read_injected(adc, rank, value);
}

int bm_hal_adc_bind_complete(const bm_hal_adc_t *adc,
                             const bm_hal_hrt_binding_t *binding) {
    if (!adc || !adc->api || !adc->api->bind_complete) {
        return BM_ERR_NOT_INIT;
    }
    if (!binding) {
        return BM_ERR_INVALID;
    }
    return adc->api->bind_complete(adc, binding);
}
