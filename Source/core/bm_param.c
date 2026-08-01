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
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-13
 *
 * @par 修改日志:
 * 2026-08-01       1.2            Codex           补齐 Doxygen 合规元数据
 *
 *    Date         Version        Author          Description
 * 2026-07-11       1.0            zeh            正式发布（批 P：bm_param 参数注册表）
 * 2026-07-11       1.1            zeh            新增 param_range_ok 值域校验，接入
 *                                                 register/set/load_overlay 三处强制
 * 2026-07-13       1.2            zeh            C6：set/get 补 name==NULL 防护，
 *                                                 消除 param_find 内 strcmp(NULL) UB
 *
 */
#include "bm/core/bm_param.h"
#include "bm/common/bm_persist.h"
#include "bm/common/bm_types.h"
#include "bm_log.h"

#include <math.h>
#include <string.h>

#define TAG "param"

/** @brief 登记的静态描述表（app 所有）。 */
static const bm_param_desc_t *s_table;
/** @brief 表项数；0 = 未登记。 */
static uint16_t s_count;
/** @brief RAM 当前值镜像。 */
static float s_vals[BM_CONFIG_PARAM_MAX];
/** @brief reset 守卫；NULL = 不拦。 */
static bm_param_reset_guard_fn_t s_guard;
/** @brief save 守卫；NULL = 不拦。 */
static bm_param_save_guard_fn_t s_save_guard;

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
    return BM_ERR_NOT_FOUND;
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

/**
 * @brief 值域校验：isfinite 恒必需；min<max 时闭区间检查；min==max 仅 isfinite。
 *
 * @param d 表项
 * @param v 候选值
 * @return 1 合法；0 拒绝
 */
static int param_range_ok(const bm_param_desc_t *d, float v)
{
    if (!isfinite(v)) {
        return 0;
    }
    if (d->min < d->max && (v < d->min || v > d->max)) {
        return 0;
    }
    return 1;
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
        if (!isfinite(table[i].min) || !isfinite(table[i].max) ||
            table[i].min > table[i].max || !param_range_ok(&table[i], table[i].def_val)) {
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
            if (!param_range_ok(&s_table[i], v)) {
                BM_LOGW(TAG, "overlay '%s' rejected: out of range/non-finite", s_table[i].pkey);
                continue; /* 坏 KV 跳过，镜像保持出厂，不计数 */
            }
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

    /* name==NULL 会使 param_find 内 strcmp 解引用空指针（UB），与框架
     * 其余公共 API 的 NULL 防护纪律对齐，入口拒绝 */
    if (name == NULL) {
        return BM_ERR_INVALID;
    }
    if (s_count == 0u) {
        return BM_ERR_NOT_INIT;
    }
    idx = param_find(name);
    if (idx < 0) {
        return BM_ERR_NOT_FOUND;
    }
    if (!param_range_ok(&s_table[idx], val)) {
        return BM_ERR_INVALID; /* 越界/非有限：不写镜像、不 apply */
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
    /* name 空指针防护同 bm_param_set */
    if (name == NULL || out == NULL) {
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
    if (s_save_guard != NULL && s_save_guard() != 0) {
        return BM_ERR_BUSY;
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
    int rc;
    float old_vals[BM_CONFIG_PARAM_MAX];
    uint8_t old_valid[BM_CONFIG_PARAM_MAX];

    if (s_count == 0u) {
        return BM_ERR_NOT_INIT;
    }
    if (s_guard != NULL && s_guard() != 0) {
        return BM_ERR_BUSY;
    }

    /* 快照现有 KV 内容：若后续 commit 失败，可回滚避免 flash/RAM 分叉。 */
    for (i = 0u; i < s_count; ++i) {
        old_valid[i] = 0;
        old_vals[i] = 0.0f;
        if (s_table[i].pkey != NULL) {
            uint16_t len = 0u;

            any_pkey = 1;
            if (bm_persist_get(s_table[i].pkey, &old_vals[i], (uint16_t)sizeof(old_vals[i]), &len) == BM_OK &&
                len == (uint16_t)sizeof(old_vals[i])) {
                old_valid[i] = 1;
            }
        }
    }

    if (any_pkey) {
        for (i = 0u; i < s_count; ++i) {
            if (s_table[i].pkey == NULL) {
                continue;
            }
            rc = bm_persist_erase(s_table[i].pkey);
            if (rc != BM_OK && rc != BM_ERR_NOT_FOUND) {
                goto rollback;
            }
        }
        rc = bm_persist_commit();
        if (rc != BM_OK) {
            goto rollback;
        }
    }

    for (i = 0u; i < s_count; ++i) {
        s_vals[i] = s_table[i].def_val;
        if ((s_table[i].flags & BM_PARAM_FLAG_REBOOT) == 0u) {
            param_apply(&s_table[i], s_vals[i]);
        }
    }
    return BM_OK;

rollback:
    for (i = 0u; i < s_count; ++i) {
        if (s_table[i].pkey != NULL && old_valid[i]) {
            (void)bm_persist_set(s_table[i].pkey, &old_vals[i], (uint16_t)sizeof(old_vals[i]));
        }
    }
    if (any_pkey) {
        (void)bm_persist_commit(); /* 尽力回滚；若仍失败只能返回原错误 */
    }
    return rc;
}

void bm_param_set_reset_guard(bm_param_reset_guard_fn_t guard)
{
    s_guard = guard;
}

void bm_param_set_save_guard(bm_param_save_guard_fn_t guard)
{
    s_save_guard = guard;
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
