/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_input_debounce.c
 * @brief 通用输入消抖组件实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增通用输入消抖组件
 */
#include "bm/component/bm_input_debounce.h"

#include <stddef.h>

int bm_input_debounce_validate_config(const bm_input_debounce_config_t *config) {
    if (config == NULL || config->stable_us == 0u) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
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
    if (deb == NULL) {
        return;
    }
    deb->state.raw = 0;
    deb->state.filtered = 0;
    deb->state.stable = 0;
    deb->state.last_edge_us = 0u;
    deb->state.last_raw_us = 0u;
    deb->state.event_count = 0u;
}

int bm_input_debounce_update(bm_input_debounce_t *deb, int raw, uint64_t now_us) {
    int changed;

    if (deb == NULL) {
        return 0;
    }

    raw = raw ? 1 : 0;

    if (raw != deb->state.raw) {
        deb->state.raw = raw;
        deb->state.last_raw_us = now_us;
        deb->state.stable = 0;
        return 0;
    }

    /* 电平未变化：检查是否已稳定超过阈值 */
    if (deb->state.stable && deb->state.filtered == raw) {
        return 0;
    }

    if ((now_us - deb->state.last_raw_us) >= deb->config.stable_us) {
        changed = (deb->state.filtered != raw) ? 1 : 0;
        deb->state.filtered = raw;
        deb->state.stable = 1;
        if (changed) {
            deb->state.last_edge_us = now_us;
            deb->state.event_count++;
            return 1;
        }
    }
    return 0;
}

int bm_input_debounce_filtered(const bm_input_debounce_t *deb) {
    if (deb == NULL) {
        return 0;
    }
    return deb->state.filtered;
}

bool bm_input_debounce_is_stable(const bm_input_debounce_t *deb) {
    if (deb == NULL) {
        return 0;
    }
    return deb->state.stable ? true : false;
}
