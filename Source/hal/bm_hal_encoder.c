/**
 * @file bm_hal_encoder.c
 * @brief 编码器 HAL 分发层（契约 → driver API）
 *
 * 将位置读取操作转发至已绑定的 driver API。
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
#include "bm_types.h"

int bm_hal_encoder_read(const bm_hal_encoder_t *enc, int32_t *value) {
    if (!enc || !enc->api || !enc->api->read) {
        return BM_ERR_NOT_INIT;
    }
    if (!value) {
        return BM_ERR_INVALID;
    }
    return enc->api->read(enc, value);
}
