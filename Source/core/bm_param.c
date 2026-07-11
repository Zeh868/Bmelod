/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_param.c
 * @brief 运行期参数注册表核心实现（批 P）
 *
 * 维护一张登记表指针（应用静态只读表所有）与一份 RAM 值镜像
 * （s_vals[BM_CONFIG_PARAM_MAX]），通过 bm_persist_get/set/erase/commit
 * 与持久化后端交互，实现 set 热写、load_overlay 上电恢复、save 落盘、
 * reset 恢复出厂默认。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-11
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-11       1.0            zeh            正式发布（批 P：bm_param 参数注册表）
 *
 */
#include "bm/core/bm_param.h"
#include "bm/common/bm_persist.h"
#include "bm/common/bm_types.h"

#include <string.h>

/** @brief 登记的静态描述表（app 所有）。 */
static const bm_param_desc_t *s_table;
/** @brief 表项数；0 = 未登记。 */
static uint16_t s_count;
/** @brief RAM 当前值镜像。 */
static float s_vals[BM_CONFIG_PARAM_MAX];
/** @brief reset 守卫；NULL = 不拦。 */
static bm_param_reset_guard_fn_t s_guard;

/**
 * @brief 按名查表。
 *
 * @param name 参数名。
 * @return 表索引；未命中返回 -1。
 */
static int param_find(const char *name)
{
    uint16_t i;

    for (i = 0u; i < s_count; ++i) {
        if (strcmp(s_table[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief 对一项执行热写落点（ptr 直写 + apply 回调，两者都给则都执行）。
 *
 * @param d 表项。
 * @param v 新值。
 */
static void param_apply(const bm_param_desc_t *d, float v)
{
    if (d->ptr != NULL) {
        *d->ptr = v;
    }
    if (d->apply != NULL) {
        d->apply(v, d->apply_user);
    }
}

int bm_param_register_table(const bm_param_desc_t *table, uint16_t count)
{
    uint16_t i;

    if (table == NULL || count == 0u) {
        return BM_ERR_INVALID;
    }
    if (count > (uint16_t)BM_CONFIG_PARAM_MAX) {
        return BM_ERR_NO_MEM;
    }
    for (i = 0u; i < count; ++i) {
        if (table[i].name == NULL ||
            (table[i].ptr == NULL && table[i].apply == NULL)) {
            return BM_ERR_INVALID;
        }
    }
    s_table = table;
    s_count = count;
    for (i = 0u; i < count; ++i) {
        s_vals[i] = table[i].def_val; /* 登记只置镜像，不 apply（init 已灌宏默认） */
    }
    return BM_OK;
}

int bm_param_load_overlay(void)
{
    uint16_t i;
    int hits = 0;

    if (s_count == 0u) {
        return BM_ERR_NOT_INIT;
    }
    for (i = 0u; i < s_count; ++i) {
        float v;
        uint16_t len = 0u;

        if (s_table[i].pkey == NULL) {
            continue;
        }
        if (bm_persist_get(s_table[i].pkey, &v, (uint16_t)sizeof(v), &len) == BM_OK &&
            len == (uint16_t)sizeof(v)) {
            s_vals[i] = v;
            param_apply(&s_table[i], v); /* REBOOT 项此处照常 apply：boot 生效点 */
            hits++;
        }
    }
    return hits;
}

int bm_param_set(const char *name, float val)
{
    int idx;

    if (s_count == 0u) {
        return BM_ERR_NOT_INIT;
    }
    idx = param_find(name);
    if (idx < 0) {
        return BM_ERR_NOT_FOUND;
    }
    s_vals[idx] = val;
    if ((s_table[idx].flags & BM_PARAM_FLAG_REBOOT) != 0u) {
        return BM_PARAM_REBOOT_REQUIRED;
    }
    param_apply(&s_table[idx], val);
    return BM_OK;
}

int bm_param_get(const char *name, float *out)
{
    int idx;

    if (s_count == 0u) {
        return BM_ERR_NOT_INIT;
    }
    if (out == NULL) {
        return BM_ERR_INVALID;
    }
    idx = param_find(name);
    if (idx < 0) {
        return BM_ERR_NOT_FOUND;
    }
    *out = s_vals[idx];
    return BM_OK;
}

int bm_param_save(void)
{
    uint16_t i;
    int saved = 0;
    int any_pkey = 0;

    if (s_count == 0u) {
        return BM_ERR_NOT_INIT;
    }
    for (i = 0u; i < s_count; ++i) {
        int rc;

        if (s_table[i].pkey == NULL) {
            continue;
        }
        any_pkey = 1;
        rc = bm_persist_set(s_table[i].pkey, &s_vals[i], (uint16_t)sizeof(s_vals[i]));
        if (rc != BM_OK) {
            return rc;
        }
        saved++;
    }
    if (!any_pkey) {
        /* 全表无 pkey 项：不触碰 persist（允许纯 RAM 用法） */
        return 0;
    }
    {
        int rc = bm_persist_commit();

        if (rc != BM_OK) {
            return rc;
        }
    }
    return saved;
}

int bm_param_reset(void)
{
    uint16_t i;
    int any_pkey = 0;

    if (s_count == 0u) {
        return BM_ERR_NOT_INIT;
    }
    if (s_guard != NULL && s_guard() != 0) {
        return BM_ERR_BUSY;
    }
    for (i = 0u; i < s_count; ++i) {
        if (s_table[i].pkey != NULL) {
            any_pkey = 1;
            break;
        }
    }
    if (any_pkey) {
        for (i = 0u; i < s_count; ++i) {
            int rc;

            if (s_table[i].pkey == NULL) {
                continue;
            }
            rc = bm_persist_erase(s_table[i].pkey);
            if (rc != BM_OK && rc != BM_ERR_NOT_FOUND) {
                return rc;
            }
        }
        {
            int rc = bm_persist_commit();

            if (rc != BM_OK) {
                return rc;
            }
        }
    }
    for (i = 0u; i < s_count; ++i) {
        s_vals[i] = s_table[i].def_val;
        if ((s_table[i].flags & BM_PARAM_FLAG_REBOOT) == 0u) {
            param_apply(&s_table[i], s_vals[i]);
        }
    }
    return BM_OK;
}

void bm_param_set_reset_guard(bm_param_reset_guard_fn_t guard)
{
    s_guard = guard;
}

uint16_t bm_param_count(void)
{
    return s_count;
}

const bm_param_desc_t *bm_param_desc_at(uint16_t idx)
{
    if (idx >= s_count) {
        return NULL;
    }
    return &s_table[idx];
}

int bm_param_value_at(uint16_t idx, float *out)
{
    if (s_count == 0u) {
        return BM_ERR_NOT_INIT;
    }
    if (out == NULL || idx >= s_count) {
        return BM_ERR_INVALID;
    }
    *out = s_vals[idx];
    return BM_OK;
}
