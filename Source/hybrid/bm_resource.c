/**
 * @file bm_resource.c
 * @brief 硬件资源声明与冲突检测实现
 *
 * 将多实例资源声明展平后检查互斥、共享读与协调访问规则。
 * @author zeh (china_qzh@163.com)
 * @version 2.0
 * @date 2026-06-12
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-11       1.1            zeh            claim_count 上界与显式兼容矩阵
 * 2026-06-12       2.0            zeh            resource_class 可扩展 ID
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm_resource.h"
#include "bm_config.h"
#include "bm_log.h"
#include "hal/bm_hal_cpu.h"

#include <stddef.h>

/** 展平后的资源声明条目 */
typedef struct {
    uintptr_t key;
    uintptr_t share_group;
    bm_resource_class_t resource_class;
    bm_resource_access_t access;
    uint32_t instance_index;
} flat_claim_t;

#ifndef BM_CONFIG_MAX_EXEC_INSTANCES
#define BM_CONFIG_MAX_EXEC_INSTANCES 16u
#endif

#ifndef BM_CONFIG_MAX_RESOURCE_CLAIMS
#define BM_CONFIG_MAX_RESOURCE_CLAIMS 64u
#endif

/**
 * @brief 判断同资源跨实例的 access 组合是否兼容
 *
 * @param a 实例 A 的访问模式
 * @param b 实例 B 的访问模式
 * @param share_a 实例 A 的 share_group
 * @param share_b 实例 B 的 share_group
 * @return 1 兼容；0 冲突
 */
static int access_pair_compatible(bm_resource_access_t a,
                                  bm_resource_access_t b,
                                  uintptr_t share_a,
                                  uintptr_t share_b) {
    if (a == BM_RESOURCE_EXCLUSIVE || b == BM_RESOURCE_EXCLUSIVE) {
        return 0;
    }
    if (a == BM_RESOURCE_OWNER && b == BM_RESOURCE_OWNER) {
        return 0;
    }
    if ((a == BM_RESOURCE_OWNER && b == BM_RESOURCE_SHARED_READ) ||
        (a == BM_RESOURCE_SHARED_READ && b == BM_RESOURCE_OWNER)) {
        return (share_a != 0u && share_a == share_b);
    }
    if (a == BM_RESOURCE_SHARED_READ && b == BM_RESOURCE_SHARED_READ) {
        return 1;
    }
    if (a == BM_RESOURCE_SHARED_COORDINATED &&
        b == BM_RESOURCE_SHARED_COORDINATED) {
        return (share_a != 0u && share_a == share_b);
    }
    return 0;
}

/**
 * @brief 判断两条资源声明是否兼容
 *
 * @param a 第一条展平声明
 * @param b 第二条展平声明
 * @return 1 兼容；0 冲突
 */
static int claims_compatible(const flat_claim_t *a, const flat_claim_t *b) {
    if (a->resource_class != b->resource_class || a->key != b->key) {
        return 1;
    }
    if (a->instance_index == b->instance_index) {
        return 1;
    }
    return access_pair_compatible(a->access, b->access,
                                  a->share_group, b->share_group);
}

/**
 * @brief 校验 owner/reader 配对与 share_group 规则
 *
 * @param flat 展平后的声明数组
 * @param flat_count 声明条目数量
 * @return BM_OK 通过；BM_ERR_INVALID 配对或 share_group 无效
 */
static int validate_owner_reader_pairs(const flat_claim_t *flat,
                                       uint32_t flat_count) {
    uint32_t i;
    uint32_t j;

    for (i = 0u; i < flat_count; ++i) {
        uint32_t owner_count = 0u;
        uint32_t reader_count = 0u;

        for (j = 0u; j < flat_count; ++j) {
            if (flat[i].resource_class != flat[j].resource_class || flat[i].key != flat[j].key) {
                continue;
            }
            if (flat[j].access == BM_RESOURCE_OWNER) {
                owner_count++;
            }
            if (flat[j].access == BM_RESOURCE_SHARED_READ) {
                reader_count++;
            }
        }

        if (reader_count > 0u) {
            if (owner_count != 1u) {
                BM_LOGE("resource", "owner/reader mismatch class=0x%04x key=%p",
                        (unsigned)flat[i].resource_class, (void *)flat[i].key);
                return BM_ERR_INVALID;
            }
            for (j = 0u; j < flat_count; ++j) {
                if (flat[i].resource_class != flat[j].resource_class || flat[i].key != flat[j].key) {
                    continue;
                }
                if (flat[j].access == BM_RESOURCE_OWNER ||
                    flat[j].access == BM_RESOURCE_SHARED_READ) {
                    if (flat[j].share_group == 0u) {
                        BM_LOGE("resource", "missing share_group class=0x%04x",
                                (unsigned)flat[j].resource_class);
                        return BM_ERR_INVALID;
                    }
                }
            }
        }
    }

    return BM_OK;
}

/**
 * @brief 拒绝同一实例对同一资源的重复声明
 *
 * @param flat 展平后的声明数组
 * @param flat_count 声明条目数量
 * @return BM_OK 无重复；BM_ERR_INVALID 存在重复
 */
static int validate_no_duplicate_claims(const flat_claim_t *flat,
                                      uint32_t flat_count) {
    uint32_t i;
    uint32_t j;

    for (i = 0u; i < flat_count; ++i) {
        for (j = i + 1u; j < flat_count; ++j) {
            if (flat[i].instance_index == flat[j].instance_index &&
                flat[i].resource_class == flat[j].resource_class &&
                flat[i].key == flat[j].key) {
                BM_LOGW("resource", "duplicate claim inst=%u class=0x%04x key=%p",
                        (unsigned)flat[i].instance_index,
                        (unsigned)flat[i].resource_class, (void *)flat[i].key);
                return BM_ERR_INVALID;
            }
        }
    }
    return BM_OK;
}

/*
 * Per-CPU 展平缓冲：bm_resource_check_conflicts 在启动期由本 CPU 独占调用，
 * 使用按 CPU 索引避免并发时 scratch buffer 损坏。
 * 非可重入，仅限主上下文调用。
 */
static flat_claim_t s_flat_claims[BM_CONFIG_CPU_COUNT][BM_CONFIG_MAX_RESOURCE_CLAIMS];

/**
 * @brief 判断资源 class 是否在框架内置范围内或为应用扩展区
 */
int bm_resource_class_valid(bm_resource_class_t resource_class) {
    if (resource_class >= BM_RESOURCE_CLASS_APP_BASE) {
        return 1;
    }
    if (resource_class >= BM_RESOURCE_CLASS_HAL_BASE &&
        resource_class <= BM_RESOURCE_CLASS_LAST_BUILTIN) {
        return 1;
    }
    return 0;
}

/**
 * @brief 检查多实例资源声明是否存在冲突
 *
 * @param claims 各实例资源声明数组的指针数组
 * @param claim_counts 各实例声明数量数组
 * @param instance_count 实例数量
 * @return BM_OK 无冲突；BM_ERR_INVALID 参数无效；BM_ERR_OVERFLOW 声明表溢出；BM_ERR_BUSY 存在冲突
 */

int bm_resource_check_conflicts(const bm_resource_claim_t *const *claims,
                                const uint32_t *claim_counts,
                                uint32_t instance_count) {
    uint32_t cpu = bm_hal_cpu_id();
    flat_claim_t *flat;
    uint32_t flat_count = 0u;
    uint32_t i;
    uint32_t j;
    uint32_t k;
    int rc;

    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return BM_ERR_INVALID;
    }
    flat = s_flat_claims[cpu];
    if (!claims || !claim_counts) {
        return BM_ERR_INVALID;
    }
    if (instance_count == 0u || instance_count > BM_CONFIG_MAX_EXEC_INSTANCES) {
        BM_LOGE("resource", "invalid instance_count=%u", (unsigned)instance_count);
        return BM_ERR_INVALID;
    }

    for (i = 0u; i < instance_count; ++i) {
        if (claim_counts[i] > 0u && !claims[i]) {
            return BM_ERR_INVALID;
        }
        if (claim_counts[i] > BM_CONFIG_MAX_RESOURCE_CLAIMS) {
            BM_LOGE("resource", "instance %u claim_count overflow",
                    (unsigned)i);
            return BM_ERR_OVERFLOW;
        }
        for (j = 0u; j < claim_counts[i]; ++j) {
            if (flat_count >= BM_CONFIG_MAX_RESOURCE_CLAIMS) {
                BM_LOGE("resource", "claim table overflow");
                return BM_ERR_OVERFLOW;
            }
            if (!bm_resource_class_valid(claims[i][j].resource_class)) {
                return BM_ERR_INVALID;
            }
            if ((unsigned)claims[i][j].access >
                (unsigned)BM_RESOURCE_SHARED_COORDINATED) {
                return BM_ERR_INVALID;
            }
            flat[flat_count].resource_class = claims[i][j].resource_class;
            flat[flat_count].key = claims[i][j].key;
            flat[flat_count].access = claims[i][j].access;
            flat[flat_count].share_group = claims[i][j].share_group;
            flat[flat_count].instance_index = i;
            flat_count++;
        }
    }

    rc = validate_no_duplicate_claims(flat, flat_count);
    if (rc != BM_OK) {
        return rc;
    }

    for (i = 0u; i < flat_count; ++i) {
        for (j = i + 1u; j < flat_count; ++j) {
            if (!claims_compatible(&flat[i], &flat[j])) {
                BM_LOGW("resource", "conflict inst %u vs %u class=0x%04x key=%p",
                        (unsigned)flat[i].instance_index,
                        (unsigned)flat[j].instance_index,
                        (unsigned)flat[i].resource_class, (void *)flat[i].key);
                return BM_ERR_BUSY;
            }
        }
    }

    for (i = 0u; i < flat_count; ++i) {
        if (flat[i].access == BM_RESOURCE_SHARED_COORDINATED) {
            for (k = 0u; k < flat_count; ++k) {
                if (i == k) {
                    continue;
                }
                if (flat[i].resource_class == flat[k].resource_class &&
                    flat[i].key == flat[k].key &&
                    flat[k].access != BM_RESOURCE_SHARED_COORDINATED) {
                    BM_LOGW("resource", "coordinated mixed access class=0x%04x",
                            (unsigned)flat[i].resource_class);
                    return BM_ERR_BUSY;
                }
            }
            if (flat[i].share_group == 0u) {
                return BM_ERR_INVALID;
            }
        }
    }

    BM_LOGD("resource", "check ok instances=%u claims=%u",
            (unsigned)instance_count, (unsigned)flat_count);
    return validate_owner_reader_pairs(flat, flat_count);
}
