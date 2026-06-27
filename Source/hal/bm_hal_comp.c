/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_comp.c
 * @brief 比较器 HAL 分发层（契约 → driver API）
 *
 * 将锁存清除等操作转发至已绑定的 driver API。
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
#include "bm_hal_comp.h"
#include "bm_types.h"

int bm_hal_comp_clear_latch(const bm_hal_comp_t *comp) {
    if (!comp || !comp->api || !comp->api->clear_latch) {
        return BM_ERR_NOT_INIT;
    }
    return comp->api->clear_latch(comp);
}
