/**
 * @file bm_mp_profile.c
 * SPDX-License-Identifier: LicenseRef-Bmeflod-Proprietary
 * @brief hard/block realtime profile 构建实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            epoch bump；log ring 门槛
 * 2026-06-15       1.2            zeh            build 后拒绝变更 profile 注册表
 *
 */
#include "bm/mp/bm_mp_profile.h"
#include "bm/mp/bm_mp_boot.h"
#include "bm/mp/bm_mp_partition.h"
#include "bm/mp/bm_mp_resource_topology.h"
#include "bm/mp/bm_mp_schedule.h"
#include "bm/common/bm_atomic_ipc.h"
#include "bm/common/bm_profile_epoch.h"
#include "bm_log.h"
#include "hal/bm_hal_cap.h"
#include "hal/bm_hal_cache.h"
#include "hal/bm_hal_timer.h"

#include <stddef.h>

#if BM_CONFIG_ENABLE_EXEC
#include "bm/hybrid/bm_exec.h"
#endif

static bm_atomic_ipc_u32_t s_profile_built;
static bm_atomic_ipc_u32_t s_profile_epoch;

#if BM_CONFIG_ENABLE_EXEC
static const bm_exec_t *const *s_profile_exec;
static uint32_t s_profile_exec_count;
#endif

static uint32_t profile_epoch_query(void) {
    return bm_atomic_ipc_load_u32(&s_profile_epoch);
}

int bm_mp_profile_is_built(void) {
    return (int)bm_atomic_ipc_load_u32(&s_profile_built);
}

void bm_mp_profile_reset(void) {
    bm_atomic_ipc_store_u32(&s_profile_built, 0u);
#if BM_CONFIG_ENABLE_EXEC
    s_profile_exec = NULL;
    s_profile_exec_count = 0u;
#endif
}

uint32_t bm_mp_profile_epoch(void) {
    return bm_atomic_ipc_load_u32(&s_profile_epoch);
}

void bm_mp_profile_bind_epoch_on_this_cpu(void) {
    if (bm_mp_profile_is_built()) {
        bm_profile_epoch_register(profile_epoch_query);
    }
}

#if BM_CONFIG_ENABLE_EXEC
void bm_mp_profile_register_exec(const bm_exec_t *const *instances,
                                 uint32_t count) {
    if (bm_mp_profile_is_built()) {
        return;
    }
    s_profile_exec = instances;
    s_profile_exec_count = count;
}
#endif

int bm_mp_profile_build(void) {
    uint8_t cpu;
    int rc;
    uint32_t schedule_mark;

    if (bm_mp_profile_is_built()) {
        return BM_ERR_ALREADY;
    }

    if (!bm_mp_partition()) {
        BM_LOGE("mp_prof", "partition not built");
        return BM_ERR_NOT_INIT;
    }

    rc = bm_mp_resource_topology_validate_table();
    if (rc != BM_OK) {
        BM_LOGE("mp_prof", "resource topology invalid rc=%d", rc);
        return rc;
    }
    schedule_mark = bm_mp_schedule_mark();

#if BM_CONFIG_MP_HARD_RT_PROFILE && BM_CONFIG_ENABLE_STREAM
    if (!bm_hal_cap_stream_profile_ok()) {
        BM_LOGE("mp_prof", "HAL stream capabilities insufficient");
        rc = BM_ERR_NOT_SUPPORTED;
        goto rollback_schedule;
    }
#endif

#if BM_CONFIG_MP_HARD_RT_PROFILE && BM_MP_MULTICORE && BM_CONFIG_ENABLE_LOG
    if (!bm_log_mp_profile_ok()) {
        BM_LOGE("mp_prof", "per-CPU log ring required for hard RT");
        rc = BM_ERR_NOT_SUPPORTED;
        goto rollback_schedule;
    }
#endif

#if BM_CONFIG_MP_HARD_RT_PROFILE && BM_MP_MULTICORE && \
    !BM_CONFIG_MP_IPC_MEMORY_COHERENT_OR_NONCACHEABLE
    if (!bm_hal_cache_is_noop()) {
        BM_LOGE("mp_prof", "MP IPC matrix requires coherent/non-cacheable memory");
        rc = BM_ERR_NOT_SUPPORTED;
        goto rollback_schedule;
    }
#endif

    for (cpu = 0u; cpu < (uint8_t)BM_CONFIG_CPU_COUNT; cpu++) {
        rc = bm_mp_schedule_register_main_loop_overhead(cpu);
        if (rc != BM_OK) {
            BM_LOGE("mp_prof", "main loop schedule cpu%u failed rc=%d",
                    (unsigned)cpu, rc);
            goto rollback_schedule;
        }
        rc = bm_mp_partition_validate_schedule(cpu, NULL);
        if (rc != BM_OK) {
            BM_LOGE("mp_prof", "schedule cpu%u failed rc=%d",
                    (unsigned)cpu, rc);
            goto rollback_schedule;
        }
    }

#if BM_CONFIG_ENABLE_EXEC && BM_CONFIG_MP_HARD_RT_PROFILE
    if (!s_profile_exec || s_profile_exec_count == 0u) {
        BM_LOGE("mp_prof", "exec table not registered for profile");
        rc = BM_ERR_NOT_INIT;
        goto rollback_schedule;
    }
    rc = bm_mp_resource_topology_validate_exec_table(
        s_profile_exec, s_profile_exec_count);
    if (rc != BM_OK) {
        BM_LOGE("mp_prof", "exec topology closure failed rc=%d", rc);
        goto rollback_schedule;
    }
#endif

#if BM_CONFIG_MP_HARD_RT_PROFILE && BM_CONFIG_ENABLE_STREAM && \
    BM_CONFIG_MP_PROFILE_STREAM_GATE_ENFORCED
    if (BM_CONFIG_MP_EXPERIMENTAL_STREAM) {
        BM_LOGE("mp_prof", "experimental stream incompatible with hard RT");
        rc = BM_ERR_NOT_SUPPORTED;
        goto rollback_schedule;
    }
#endif

    {
        uint32_t epoch = bm_atomic_ipc_inc_u32(&s_profile_epoch);
        if (epoch == 0u) {
            bm_atomic_ipc_store_u32(&s_profile_epoch, 1u);
        }
    }
    bm_hal_timer_bump_all_clock_epochs();
    bm_profile_epoch_register(profile_epoch_query);
    bm_atomic_ipc_store_u32(&s_profile_built, 1u);
#if BM_MP_MULTICORE && BM_CONFIG_MP_HARD_RT_PROFILE
    rc = bm_mp_boot_signal_profile_ready();
    if (rc != BM_OK) {
        bm_atomic_ipc_store_u32(&s_profile_built, 0u);
        bm_mp_schedule_restore(schedule_mark);
        return rc;
    }
#endif
    BM_LOGI("mp_prof", "profile build ok epoch=%u hard_rt=%u",
            (unsigned)bm_mp_profile_epoch(),
            (unsigned)BM_CONFIG_MP_HARD_RT_PROFILE);
    return BM_OK;

rollback_schedule:
    bm_mp_schedule_restore(schedule_mark);
    return rc;
}
