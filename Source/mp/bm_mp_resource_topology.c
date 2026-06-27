/**
 * @file bm_mp_resource_topology.c
 * SPDX-License-Identifier: LicenseRef-Bmeflod-Proprietary
 * @brief 资源拓扑注册与亲和闭包校验实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-06-15       1.1            zeh            partition build 后冻结拓扑注册表
 * 2026-06-19       1.2            zeh            补 bm_mp_profile.h 桥接宏，使 hard
 *                                               RT 未注册 HAL claim fail-closed 生效
 *
 */
#include "bm/mp/bm_mp_resource_topology.h"
#include "bm/mp/bm_mp_partition.h"
#include "bm/mp/bm_mp_profile.h"
#include "bm_log.h"

#include <string.h>

static bm_mp_resource_topology_entry_t
    s_entries[BM_CONFIG_MP_RESOURCE_TOPOLOGY_MAX];
static uint32_t s_entry_count;

static int mask_has_single_cpu(uint32_t mask, uint8_t *cpu_out) {
    uint32_t i;
    uint32_t count = 0u;
    uint8_t sole = 0u;

    for (i = 0u; i < BM_CONFIG_CPU_COUNT; i++) {
        if ((mask & (1u << i)) != 0u) {
            count++;
            sole = (uint8_t)i;
        }
    }
    if (count != 1u) {
        return 0;
    }
    if (cpu_out) {
        *cpu_out = sole;
    }
    return 1;
}

void bm_mp_resource_topology_reset(void) {
    memset(s_entries, 0, sizeof(s_entries));
    s_entry_count = 0u;
}

int bm_mp_resource_topology_register(
    const bm_mp_resource_topology_entry_t *entry) {
    if (bm_mp_partition() != NULL) {
        return BM_ERR_BUSY;
    }
    if (!entry || entry->allowed_cpu_mask == 0u) {
        return BM_ERR_INVALID;
    }
    if (s_entry_count >= BM_CONFIG_MP_RESOURCE_TOPOLOGY_MAX) {
        return BM_ERR_NO_MEM;
    }
    s_entries[s_entry_count++] = *entry;
    return BM_OK;
}

int bm_mp_resource_topology_validate_claim(uint8_t owner_cpu,
                                           const bm_resource_claim_t *claim) {
    uint32_t i;
    uint8_t allowed_cpu;

    if (!claim || owner_cpu >= BM_CONFIG_CPU_COUNT) {
        return BM_ERR_INVALID;
    }

    for (i = 0u; i < s_entry_count; i++) {
        const bm_mp_resource_topology_entry_t *e = &s_entries[i];

        if (e->resource_class != claim->resource_class ||
            e->key != claim->key) {
            continue;
        }
        if ((e->allowed_cpu_mask & (1u << owner_cpu)) == 0u) {
            BM_LOGE("mp_rtopo", "claim %s owner cpu%u not in mask 0x%x",
                    claim->name ? claim->name : "?",
                    (unsigned)owner_cpu,
                    (unsigned)e->allowed_cpu_mask);
            return BM_ERR_INVALID;
        }
        if (!mask_has_single_cpu(e->allowed_cpu_mask, &allowed_cpu)) {
            BM_LOGE("mp_rtopo", "ambiguous mask 0x%x class=0x%x key=0x%x",
                    (unsigned)e->allowed_cpu_mask,
                    (unsigned)e->resource_class,
                    (unsigned)e->key);
            return BM_ERR_INVALID;
        }
        if (allowed_cpu != owner_cpu) {
            return BM_ERR_INVALID;
        }
        return BM_OK;
    }

    /* 未注册拓扑的纯软件 claim 由 exec owner 自行负责 */
    if (claim->resource_class >= BM_RESOURCE_CLASS_APP_BASE) {
        return BM_OK;
    }
    if (s_entry_count == 0u) {
        return BM_OK;
    }
#if BM_CONFIG_MP_HARD_RT_PROFILE
    BM_LOGE("mp_rtopo", "unregistered HAL claim class=0x%x key=0x%x",
            (unsigned)claim->resource_class, (unsigned)claim->key);
    return BM_ERR_INVALID;
#else
    BM_LOGW("mp_rtopo", "unregistered HAL claim class=0x%x key=0x%x",
            (unsigned)claim->resource_class, (unsigned)claim->key);
    return BM_OK;
#endif
}

int bm_mp_resource_topology_validate_table(void) {
    uint32_t i;

    for (i = 0u; i < s_entry_count; i++) {
        const bm_mp_resource_topology_entry_t *e = &s_entries[i];
        uint8_t cpu;

        if (!mask_has_single_cpu(e->allowed_cpu_mask, &cpu)) {
            BM_LOGE("mp_rtopo", "entry %u ambiguous mask 0x%x",
                    (unsigned)i, (unsigned)e->allowed_cpu_mask);
            return BM_ERR_INVALID;
        }
        if (cpu >= BM_CONFIG_CPU_COUNT) {
            return BM_ERR_INVALID;
        }
    }
    return BM_OK;
}

#if BM_CONFIG_ENABLE_EXEC
#include "bm/hybrid/bm_exec.h"

int bm_mp_resource_topology_validate_exec_table(
    const bm_exec_t *const *instances, uint32_t count) {
    uint32_t i;
    uint32_t c;

    if (!instances && count > 0u) {
        return BM_ERR_INVALID;
    }
    for (i = 0u; i < count; i++) {
        const bm_exec_t *inst = instances[i];
        uint32_t s;

        if (!inst || inst->owner_cpu >= BM_CONFIG_CPU_COUNT) {
            return BM_ERR_INVALID;
        }
        if (inst->claims) {
            for (c = 0u; c < inst->claim_count; c++) {
                int rc = bm_mp_resource_topology_validate_claim(
                    inst->owner_cpu, &inst->claims[c]);
                if (rc != BM_OK) {
                    return rc;
                }
            }
        }
        for (s = 0u; s < inst->slot_count; s++) {
            const bm_exec_slot_t *slot = &inst->slots[s];
            if ((slot->kind == BM_EXEC_SLOT_BLOCK ||
                 slot->kind == BM_EXEC_SLOT_FRAME) &&
                slot->stream != NULL &&
                slot->stream->owner_cpu != inst->owner_cpu) {
                BM_LOGE("mp_rtopo", "exec %s stream owner mismatch",
                        inst->name ? inst->name : "?");
                return BM_ERR_INVALID;
            }
        }
    }
    return BM_OK;
}
#endif /* BM_CONFIG_ENABLE_EXEC */
