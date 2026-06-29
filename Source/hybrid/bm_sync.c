/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_sync.c
 * @brief 控制实例硬件同步域实现
 *
 * 校验域描述后委托 HAL 完成配置、武装与触发。
 * @author zeh (china_qzh@163.com)
 * @version 1.4
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-11       1.1            zeh            域切换清理与框架层 armed 状态
 * 2026-06-11       1.2            zeh            member_count 上界校验
 * 2026-06-11       1.3            zeh            phase_ticks 上界校验
 * 2026-06-15       1.4            zeh            配置后拒绝动态切换同步域
 *
 */
#include "bm_sync.h"
#include "bm_config.h"
#include "bm/core/bm_cpu_local.h"
#include "bm/core/bm_cpu_local.h"
#include "bm_critical_wrap.h"
#include "bm_sync_hal.h"
#include "bm_log.h"

/** 按 CPU 分域的同步域运行时状态 */
typedef struct {
    const bm_sync_domain_t *active_domain;
    bm_sync_state_t         state;
} bm_sync_cpu_state_t;

typedef struct {
    bm_sync_cpu_state_t state;
    uint8_t padding[BM_CONFIG_CACHE_LINE -
                    (sizeof(bm_sync_cpu_state_t) % BM_CONFIG_CACHE_LINE)];
} bm_sync_cpu_storage_t;

static BM_CACHE_ALIGNAS(BM_CONFIG_CACHE_LINE)
bm_sync_cpu_storage_t g_sync_cpu[BM_CONFIG_CPU_COUNT];

/**
 * @brief 获取当前核同步域状态指针
 */
static bm_sync_cpu_state_t *sync_this(void) {
    uint32_t cpu = BM_CPU_THIS();

    /* CPU 越界时返回 NULL，调用方统一 fail-closed */
    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return NULL;
    }
    return &g_sync_cpu[cpu].state;
}

#define g_active_domain  (sync_this()->active_domain)
#define g_state          (sync_this()->state)

/**
 * @brief 原子提交活动域和公开状态（本核）
 */
static void sync_set_state(const bm_sync_domain_t *domain,
                           bm_sync_state_t state) {
    bm_irq_state_t irq_state = BM_CRITICAL_ENTER();

    g_active_domain = domain;
    g_state = state;
    BM_CRITICAL_EXIT(irq_state);
}

/**
 * @brief 临界区内一次性快照活动域与状态，避免 TOCTOU
 */
static void sync_snapshot(const bm_sync_domain_t **domain_out,
                          bm_sync_state_t *state_out) {
    bm_irq_state_t irq_state = BM_CRITICAL_ENTER();

    if (domain_out) {
        *domain_out = g_active_domain;
    }
    if (state_out) {
        *state_out = g_state;
    }
    BM_CRITICAL_EXIT(irq_state);
}

/**
 * @brief 校验同步域描述符字段完整性
 *
 * @param domain 同步域描述符指针
 * @return BM_OK 有效；BM_ERR_INVALID 参数无效
 */
static int validate_domain(const bm_sync_domain_t *domain) {
    uint32_t i;
    uint32_t j;
#if BM_CPU_LOCAL_ENABLE_ROUTE
    uint8_t owner_cpu = 0u;
    int have_owner = 0;
#endif

    if (!domain || !domain->master_timer || !domain->members ||
        !domain->phase_ticks || domain->member_count == 0u) {
        BM_LOGE("sync", "invalid domain descriptor");
        return BM_ERR_INVALID;
    }
    if (domain->member_count > BM_CONFIG_MAX_SYNC_MEMBERS) {
        BM_LOGE("sync", "member_count overflow %u",
                (unsigned)domain->member_count);
        return BM_ERR_INVALID;
    }
    for (i = 0u; i < domain->member_count; ++i) {
        if (!domain->members[i]) {
            BM_LOGE("sync", "null member at index %u", (unsigned)i);
            return BM_ERR_INVALID;
        }
        if (domain->phase_ticks[i] > BM_CONFIG_SYNC_MAX_PHASE_TICKS) {
            BM_LOGE("sync", "phase_ticks[%u] overflow %u",
                    (unsigned)i, (unsigned)domain->phase_ticks[i]);
            return BM_ERR_INVALID;
        }
#if BM_CPU_LOCAL_ENABLE_ROUTE
        if (!have_owner) {
            owner_cpu = domain->members[i]->owner_cpu;
            have_owner = 1;
        } else if (domain->members[i]->owner_cpu != owner_cpu) {
            BM_LOGE("sync", "sync members must stay on the same cpu when routed");
            return BM_ERR_NOT_SUPPORTED;
        }
#endif
        for (j = i + 1u; j < domain->member_count; ++j) {
            if (domain->members[i] == domain->members[j]) {
                BM_LOGE("sync", "duplicate member at index %u",
                        (unsigned)j);
                return BM_ERR_INVALID;
            }
        }
    }
#if BM_CPU_LOCAL_ENABLE_ROUTE
    /* 使用统一 owner 守卫原语代替手写范围 + 核号比较，语义等价：
     * !have_owner → 未获得任何 owner_cpu（member_count>0 时不应发生）；
     * !bm_cpu_is_owner(owner_cpu) → 当前核不是该域的 owner 核（多核强制）。 */
    if (!have_owner || !bm_cpu_is_owner(owner_cpu)) {
        BM_LOGE("sync", "sync domain must be operated by its owner cpu");
        return BM_ERR_INVALID;
    }
#endif
    return BM_OK;
}

/**
 * @brief 配置同步域并委托 HAL 完成硬件设置
 *
 * @param domain 同步域描述符指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；其他为 HAL 错误码
 */
int bm_sync_configure(const bm_sync_domain_t *domain) {
    const bm_sync_domain_t *previous_domain;
    int rc = validate_domain(domain);

    if (rc != BM_OK) {
        return rc;
    }

    sync_snapshot(&previous_domain, NULL);
    if (previous_domain != NULL && previous_domain != domain) {
        return BM_ERR_BUSY;
    }
    sync_set_state(previous_domain, BM_SYNC_STATE_TRANSITION);

    rc = bm_sync_hal_configure(domain);
    if (rc != BM_OK) {
        BM_LOGE("sync", "hal configure failed rc=%d", rc);
        bm_sync_hal_safe_stop(domain);
        sync_set_state(NULL, BM_SYNC_STATE_IDLE);
        return rc;
    }

    sync_set_state(domain, BM_SYNC_STATE_CONFIGURED);
    BM_LOGI("sync", "configured %u members", (unsigned)domain->member_count);
    return BM_OK;
}

/**
 * @brief 武装已配置的同步域，准备触发
 *
 * @param domain 同步域描述符指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_NOT_INIT 未先配置；其他为 HAL 错误码
 */
int bm_sync_arm(const bm_sync_domain_t *domain) {
    const bm_sync_domain_t *active;
    bm_sync_state_t state;
    int rc = validate_domain(domain);

    if (rc != BM_OK) {
        return rc;
    }

    sync_snapshot(&active, &state);
    if (active != domain || state != BM_SYNC_STATE_CONFIGURED) {
        BM_LOGW("sync", "arm before configure");
        return BM_ERR_NOT_INIT;
    }

    sync_set_state(domain, BM_SYNC_STATE_TRANSITION);
    rc = bm_sync_hal_arm(domain);
    if (rc != BM_OK) {
        BM_LOGE("sync", "hal arm failed rc=%d", rc);
        bm_sync_hal_safe_stop(domain);
        sync_set_state(NULL, BM_SYNC_STATE_IDLE);
    } else {
        sync_set_state(domain, BM_SYNC_STATE_ARMED);
        BM_LOGI("sync", "armed");
    }
    return rc;
}

/**
 * @brief 触发同步域，启动成员定时器对齐
 *
 * @param domain 同步域描述符指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_NOT_INIT 未先配置或未武装；其他为 HAL 错误码
 */
int bm_sync_trigger(const bm_sync_domain_t *domain) {
    const bm_sync_domain_t *active;
    bm_sync_state_t state;
    int rc = validate_domain(domain);

    if (rc != BM_OK) {
        return rc;
    }

    sync_snapshot(&active, &state);
    if (active != domain) {
        BM_LOGW("sync", "trigger before configure");
        return BM_ERR_NOT_INIT;
    }
    if (state != BM_SYNC_STATE_ARMED) {
        BM_LOGW("sync", "trigger before arm");
        return BM_ERR_NOT_INIT;
    }

    sync_set_state(domain, BM_SYNC_STATE_TRANSITION);
    rc = bm_sync_hal_trigger(domain);
    if (rc != BM_OK) {
        BM_LOGE("sync", "hal trigger failed rc=%d", rc);
        bm_sync_hal_safe_stop(domain);
        sync_set_state(NULL, BM_SYNC_STATE_IDLE);
    } else {
        sync_set_state(domain, BM_SYNC_STATE_CONFIGURED);
        BM_LOGD("sync", "triggered");
    }
    return rc;
}

/**
 * @brief 安全停止同步域并清除活动域记录
 *
 * @param domain 同步域描述符指针（可为 NULL，仅清除活动域）
 */
void bm_sync_safe_stop(const bm_sync_domain_t *domain) {
    const bm_sync_domain_t *target;

    sync_snapshot(&target, NULL);

    /*
     * safe_stop 始终停止当前活动域（target）。
     * domain 参数作为安全校验提示：若传入 domain 与活动域不匹配，
     * 仅告警并继续停止 target。这不是 API 错误——调用方可能
     * 在 domain 已变更后仍持有旧的 domain 引用。
     */
    if (!target) {
        target = domain;
    } else if (domain && domain != target) {
        BM_LOGW("sync", "safe_stop: domain mismatch, stopping active domain"
                " (caller had %p, active is %p)",
                (const void *)domain, (const void *)target);
    }

    sync_set_state(target, BM_SYNC_STATE_TRANSITION);
    if (target) {
        bm_sync_hal_safe_stop(target);
    }
    sync_set_state(NULL, BM_SYNC_STATE_IDLE);
    BM_LOGI("sync", "safe stop");
}

/**
 * @brief 获取当前同步域状态
 *
 * @return 当前状态快照
 */
bm_sync_state_t bm_sync_get_state(void) {
    bm_sync_cpu_state_t *s = sync_this();
    bm_sync_state_t state;

    if (!s) {
        return BM_SYNC_STATE_IDLE;
    }
    {
        bm_irq_state_t irq_state = BM_CRITICAL_ENTER();
        state = s->state;
        BM_CRITICAL_EXIT(irq_state);
    }
    return state;
}
