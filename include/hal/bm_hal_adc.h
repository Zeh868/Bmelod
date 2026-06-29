/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_adc.h
 * @brief ADC HAL 接口
 *
 * 支持注入通道读取及与 HRT 绑定的转换完成回调。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 *
 */
#ifndef BM_HAL_ADC_H
#define BM_HAL_ADC_H

#include "drv/bm_drv_adc.h"
#include "hal/bm_hal_hrt.h"
#include "bm/common/bm_types.h"

typedef struct bm_hal_adc bm_hal_adc_t;

/**
 * @brief 读取注入组指定 rank 的采样值
 *
 * @param adc ADC 实例指针
 * @param rank 注入通道序号
 * @param value 输出采样值指针
 * @return BM_OK 成功；否则为平台错误码
 */
int bm_hal_adc_read_injected(const bm_hal_adc_t *adc,
                             uint32_t rank, uint16_t *value);

/**
 * @brief 绑定 ADC 转换完成事件到 HRT 回调
 *
 * @param adc ADC 实例指针
 * @param binding HRT 绑定参数
 * @return BM_OK 成功；否则为平台错误码
 */
int bm_hal_adc_bind_complete(const bm_hal_adc_t *adc,
                             const bm_hal_hrt_binding_t *binding);

#endif /* BM_HAL_ADC_H */
