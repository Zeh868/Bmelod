/**
 * @file bm_drv_adc.h
 * @brief ADC 设备驱动 API
 */
#ifndef BM_DRV_ADC_H
#define BM_DRV_ADC_H

#include "drv/bm_drv.h"
#include "hal/bm_hal_hrt.h"
#include "bm/common/bm_types.h"

#include <stdint.h>

struct bm_hal_adc;

struct bm_adc_driver_api {
    int (*read_injected)(const struct bm_hal_adc *dev, uint32_t rank, uint16_t *value);
    int (*bind_complete)(const struct bm_hal_adc *dev,
                         const bm_hal_hrt_binding_t *binding);
};

struct bm_hal_adc {
    const struct bm_adc_driver_api *api;
    const void                     *config;
};

#endif /* BM_DRV_ADC_H */
