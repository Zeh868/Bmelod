/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_input_debounce.c
 * @brief 通用输入消抖组件实现
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增通用输入消抖组件
 * 2026-07-28       1.1            zeh            复用 bm/common 防抖纯算法
 */
#include "bm/component/bm_input_debounce.h"

#include <stddef.h>

int bm_input_debounce_validate_config(const bm_input_debounce_config_t *config) {
    return bm_input_debounce_common_validate_config(config);
}

int bm_input_debounce_init(bm_input_debounce_t *deb) {
    if (deb == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_input_debounce_validate_config(&deb->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_input_debounce_reset(deb);
    return BM_OK;
}

void bm_input_debounce_reset(bm_input_debounce_t *deb) {
    bm_input_debounce_common_reset(deb);
}

int bm_input_debounce_update(bm_input_debounce_t *deb, int raw, uint64_t now_us) {
    return bm_input_debounce_common_update(deb, raw, now_us);
}

int bm_input_debounce_filtered(const bm_input_debounce_t *deb) {
    return bm_input_debounce_common_filtered(deb);
}

bool bm_input_debounce_is_stable(const bm_input_debounce_t *deb) {
    return bm_input_debounce_common_is_stable(deb);
}
