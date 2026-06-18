/**
 * @file bm_hal_encoder.h
 * @brief 编码器 HAL 接口
 *
 * 读取增量式编码器计数值，具体硬件由平台实现绑定。
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
#ifndef BM_HAL_ENCODER_H
#define BM_HAL_ENCODER_H

#include "drv/bm_drv_encoder.h"
#include "bm/common/bm_types.h"

typedef struct bm_hal_encoder bm_hal_encoder_t;

/**
 * @brief 读取编码器当前计数值
 *
 * @param enc 编码器实例指针
 * @param value 输出计数值指针
 * @return BM_OK 成功；否则为平台错误码
 */
int bm_hal_encoder_read(const bm_hal_encoder_t *enc, int32_t *value);

#endif /* BM_HAL_ENCODER_H */
