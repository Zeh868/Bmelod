/**
 * @file bm_ext_algorithm.c
 * @brief K3 外部算法注册表桩实现（最多 8 项静态绑定）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            初始注册表桩
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/extensions/bm_ext_algorithm.h"
#include "bm/common/bm_types.h"

#include <stddef.h>
#include <string.h>

static const bm_ext_algorithm_descriptor_t *s_registry[BM_EXT_ALGORITHM_MAX];
static uint32_t s_count;

static int names_equal(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return 0;
    }
    return strcmp(a, b) == 0;
}

int bm_ext_algorithm_register(const bm_ext_algorithm_descriptor_t *desc) {
    uint32_t i;

    if (desc == NULL || desc->name == NULL || desc->init == NULL ||
        desc->step == NULL || desc->fini == NULL) {
        return BM_ERR_INVALID;
    }

    for (i = 0u; i < s_count; ++i) {
        if (s_registry[i] != NULL && names_equal(s_registry[i]->name, desc->name)) {
            return BM_ERR_ALREADY;
        }
    }

    if (s_count >= BM_EXT_ALGORITHM_MAX) {
        return BM_ERR_NO_MEM;
    }

    s_registry[s_count] = desc;
    s_count++;
    return BM_OK;
}

const bm_ext_algorithm_descriptor_t *bm_ext_algorithm_find(const char *name) {
    uint32_t i;

    if (name == NULL) {
        return NULL;
    }

    for (i = 0u; i < s_count; ++i) {
        if (s_registry[i] != NULL && names_equal(s_registry[i]->name, name)) {
            return s_registry[i];
        }
    }
    return NULL;
}

uint32_t bm_ext_algorithm_count(void) {
    return s_count;
}

const bm_ext_algorithm_descriptor_t *bm_ext_algorithm_get(uint32_t index) {
    if (index >= s_count) {
        return NULL;
    }
    return s_registry[index];
}

void bm_ext_algorithm_reset(void) {
    uint32_t i;

    for (i = 0u; i < BM_EXT_ALGORITHM_MAX; ++i) {
        s_registry[i] = NULL;
    }
    s_count = 0u;
}
