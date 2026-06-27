/**
 * @file bm_mp_partition.c
 * SPDX-License-Identifier: LicenseRef-Bmeflod-Proprietary
 * @brief 静态分区表构建与校验
 *
 * 依据 event/module owner 预指定与 round-robin 策略生成只读 `bm_mp_partition_t`。
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
#include "bm/mp/bm_mp_partition.h"
#include "bm/mp/bm_mp_cpu.h"
#include "bm/mp/bm_mp_types.h"
#include "bm/mp/bm_mp_resource_topology.h"
#include "bm/common/bm_crc32.h"
#include "bm/core/bm_module.h"
#include "bm_log.h"

#if BM_CONFIG_ENABLE_MODULE
#include "bm_module.h"
#endif

#include <string.h>

/**
 * @brief 获取当前构建下的模块表指针
 *
 * @return 模块表首地址；未启用模块时返回 NULL
 */
static const bm_module_t *const *partition_module_table(void) {
#if BM_CONFIG_ENABLE_MODULE
    return bm_module_table();
#else
    return NULL;
#endif
}

/**
 * @brief 获取当前构建下的模块表大小
 *
 * @return 模块条目数；未启用模块时返回 0
 */
static uint32_t partition_module_count(void) {
#if BM_CONFIG_ENABLE_MODULE
    return bm_module_count();
#else
    return 0u;
#endif
}

/** boot 前登记的事件 owner 预指定（BM_CPU_ANY 表示待分区器分配） */
static uint8_t s_event_owner_decl[BM_CONFIG_MAX_EVENT_TYPES];
static const char *s_event_name[BM_CONFIG_MAX_EVENT_TYPES];
static uint8_t s_event_owner[BM_CONFIG_MAX_EVENT_TYPES];
static uint8_t s_module_owner[BM_CONFIG_MAX_MODULES];

typedef struct {
    uint32_t        module_index;
    bm_event_type_t event_type;
} bm_mp_module_event_binding_t;

static bm_mp_module_event_binding_t
    s_module_event_bindings[BM_CONFIG_MAX_MODULES * 4u];
static uint32_t s_module_event_binding_count;

static bm_mp_partition_t s_partition;
static int s_partition_built;
static int s_owner_decl_initialized;

/**
 * @brief 回滚分区构建期间的可见状态。
 *
 * 失败路径必须同时撤销 built 标记与模块 owner resolver，避免外层重试时
 * 继续观察到半成品分区。
 */
static void partition_rollback_state(void) {
    s_partition_built = 0;
#if BM_CONFIG_ENABLE_MODULE
    bm_module_set_owner_resolver(NULL);
#endif
}

static void partition_owner_decl_init(void) {
    if (!s_owner_decl_initialized) {
        memset(s_event_owner_decl, BM_CPU_ANY, sizeof(s_event_owner_decl));
        s_owner_decl_initialized = 1;
    }
}

void bm_mp_partition_reset(void) {
    uint32_t i;

    memset(&s_partition, 0, sizeof(s_partition));
    memset(s_event_owner_decl, BM_CPU_ANY, sizeof(s_event_owner_decl));
    for (i = 0u; i < BM_CONFIG_MAX_EVENT_TYPES; i++) {
        s_event_name[i] = NULL;
    }
    memset(s_event_owner, 0, sizeof(s_event_owner));
    memset(s_module_owner, 0, sizeof(s_module_owner));
    s_module_event_binding_count = 0u;
    s_partition_built = 0;
    s_owner_decl_initialized = 1;
#if BM_CONFIG_ENABLE_MODULE
    bm_module_set_owner_resolver(NULL);
#endif
}

static uint32_t partition_crc32(const uint8_t *data, uint32_t len) {
    return bm_crc32(data, len);
}

int bm_mp_partition_register_event_owner(bm_event_type_t type,
                                         const char *name,
                                         uint8_t owner_cpu) {
    partition_owner_decl_init();
    if (s_partition_built || type >= BM_CONFIG_MAX_EVENT_TYPES || !name ||
        (owner_cpu != BM_CPU_ANY && owner_cpu >= BM_CONFIG_CPU_COUNT)) {
        return BM_ERR_INVALID;
    }
    if (s_event_name[type] != NULL) {
        return BM_ERR_ALREADY;
    }
    s_event_name[type] = name;
    s_event_owner_decl[type] = owner_cpu;
    return BM_OK;
}

int bm_mp_partition_register_module_event(uint32_t module_index,
                                          bm_event_type_t type) {
    if (s_partition_built || module_index >= BM_CONFIG_MAX_MODULES ||
        type >= BM_CONFIG_MAX_EVENT_TYPES) {
        return BM_ERR_INVALID;
    }
    if (s_module_event_binding_count >=
        (uint32_t)(sizeof(s_module_event_bindings) /
                   sizeof(s_module_event_bindings[0]))) {
        return BM_ERR_NO_MEM;
    }
    s_module_event_bindings[s_module_event_binding_count].module_index =
        module_index;
    s_module_event_bindings[s_module_event_binding_count].event_type = type;
    s_module_event_binding_count++;
    return BM_OK;
}

static int partition_validate_module_event_closure(void) {
    uint32_t i;

    /*
     * 模块-事件闭包：模块订阅的事件 owner 必须与模块 owner 一致，
     * 否则跨核 IPC 转发后回调会在错误 CPU 上执行，破坏 PERCPU 契约。
     */
    for (i = 0u; i < s_module_event_binding_count; i++) {
        const bm_mp_module_event_binding_t *b = &s_module_event_bindings[i];
        if (b->module_index >= partition_module_count() ||
            b->module_index >= BM_CONFIG_MAX_MODULES) {
            return BM_ERR_INVALID;
        }
        if (b->event_type >= BM_CONFIG_MAX_EVENT_TYPES) {
            return BM_ERR_INVALID;
        }
        if (s_module_owner[b->module_index] != s_event_owner[b->event_type]) {
            BM_LOGE("mp_part",
                    "module %u event %u owner mismatch mod_cpu=%u evt_cpu=%u",
                    (unsigned)b->module_index,
                    (unsigned)b->event_type,
                    (unsigned)s_module_owner[b->module_index],
                    (unsigned)s_event_owner[b->event_type]);
            return BM_ERR_INVALID;
        }
    }
    for (i = 0u; i < partition_module_count() &&
                    i < BM_CONFIG_MAX_MODULES; i++) {
        const bm_module_t *mod = partition_module_table()[i];
        uint32_t e;

        if (mod == NULL) {
            continue;
        }
        if (mod->subscribed_events == NULL ||
            mod->subscribed_event_count == 0u) {
            continue;
        }
        for (e = 0u; e < (uint32_t)mod->subscribed_event_count; e++) {
            bm_event_type_t et = mod->subscribed_events[e];

            if (et >= BM_CONFIG_MAX_EVENT_TYPES) {
                BM_LOGE("mp_part", "module %u event %u out of range",
                        (unsigned)i, (unsigned)et);
                return BM_ERR_INVALID;
            }
            if (s_module_owner[i] != s_event_owner[et]) {
                BM_LOGE("mp_part",
                        "module %s event %u owner mismatch mod_cpu=%u evt_cpu=%u",
                        mod->name ? mod->name : "?",
                        (unsigned)et,
                        (unsigned)s_module_owner[i],
                        (unsigned)s_event_owner[et]);
                return BM_ERR_INVALID;
            }
        }
    }
    return BM_OK;
}

int bm_mp_partition_build_and_validate(void) {
    uint32_t cpu_count = BM_CONFIG_CPU_COUNT;
    uint32_t rr = 0u;
    uint32_t i;
    int rc;

    if (s_partition_built) {
        return BM_OK;
    }
    partition_owner_decl_init();
    partition_rollback_state();

    memset(s_event_owner, 0, sizeof(s_event_owner));
    memset(s_module_owner, 0, sizeof(s_module_owner));

    for (i = 0u; i < BM_CONFIG_MAX_EVENT_TYPES; i++) {
        if (s_event_owner_decl[i] == BM_CPU_ANY) {
            s_event_owner[i] = (uint8_t)(i % cpu_count);
        } else if (s_event_owner_decl[i] < cpu_count) {
            s_event_owner[i] = s_event_owner_decl[i];
        } else {
            BM_LOGE("mp_part", "event %u invalid owner decl %u",
                    (unsigned)i, (unsigned)s_event_owner_decl[i]);
            return BM_ERR_INVALID;
        }
    }

    rr = 0u;
    for (i = 0u; i < partition_module_count() &&
                    i < BM_CONFIG_MAX_MODULES; i++) {
        const bm_module_t *mod = partition_module_table()[i];

        if (mod != NULL && mod->owner_cpu != BM_CPU_ANY &&
            mod->owner_cpu < cpu_count) {
            s_module_owner[i] = mod->owner_cpu;
        } else {
            s_module_owner[i] = (uint8_t)(rr % cpu_count);
            rr++;
        }
    }

    s_partition.cpu_count = cpu_count;
    s_partition.layout_version = BM_MP_PARTITION_LAYOUT_VERSION;
    s_partition.event_owner = s_event_owner;
    s_partition.module_owner = s_module_owner;
    s_partition.partition_crc = partition_crc32(
        (const uint8_t *)&cpu_count, (uint32_t)sizeof(cpu_count));
    s_partition.partition_crc ^= partition_crc32(
        (const uint8_t *)&s_partition.layout_version,
        (uint32_t)sizeof(s_partition.layout_version));
    s_partition.partition_crc ^= partition_crc32(
        s_event_owner, (uint32_t)sizeof(s_event_owner));
    s_partition.partition_crc ^= partition_crc32(
        s_module_owner, (uint32_t)sizeof(s_module_owner));

    s_partition_built = 1;
#if BM_CONFIG_ENABLE_MODULE
    bm_module_set_owner_resolver(bm_mp_module_owner);
#endif
    rc = bm_mp_resource_topology_validate_table();
    if (rc != BM_OK) {
        partition_rollback_state();
        return rc;
    }
    rc = partition_validate_module_event_closure();
    if (rc != BM_OK) {
        partition_rollback_state();
        return rc;
    }
    BM_LOGI("mp_part", "partition built cpus=%u crc=0x%08x",
            (unsigned)cpu_count, (unsigned)s_partition.partition_crc);
    return BM_OK;
}

int bm_mp_partition_register_events_on_this_cpu(void) {
    uint32_t cpu = BM_CPU_THIS();
    uint32_t i;

    if (!s_partition_built || cpu >= BM_CONFIG_CPU_COUNT) {
        return BM_ERR_NOT_INIT;
    }
    for (i = 0u; i < BM_CONFIG_MAX_EVENT_TYPES; i++) {
        int rc;

        if (s_event_name[i] == NULL || s_event_owner[i] != (uint8_t)cpu) {
            continue;
        }
        rc = bm_event_register_type((bm_event_type_t)i, s_event_name[i]);
        if (rc != BM_OK) {
            return rc;
        }
    }
    return BM_OK;
}

const bm_mp_partition_t *bm_mp_partition(void) {
    return s_partition_built ? &s_partition : NULL;
}

uint8_t bm_mp_event_owner(bm_event_type_t type) {
    if (type >= BM_CONFIG_MAX_EVENT_TYPES || !s_partition_built) {
        return BM_CPU_ANY;
    }
    return s_event_owner[type];
}

uint8_t bm_mp_module_owner(uint32_t module_index) {
    if (module_index >= partition_module_count() || !s_partition_built) {
        return BM_CPU_ANY;
    }
    return s_module_owner[module_index];
}
