/**
 * @file bm_mp_schedule.c
 * SPDX-License-Identifier: LicenseRef-Bmeflod-Proprietary
 * @brief WCET / 响应时间静态校验实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-06-15       1.1            zeh            profile build 后冻结 schedule 注册表
 *
 */
#include "bm/mp/bm_mp_schedule.h"
#include "bm/mp/bm_mp_profile.h"
#include "bm_config.h"
#include "bm_log.h"

#include <string.h>

static bm_mp_schedule_slot_t s_slots[BM_CONFIG_MP_SCHEDULE_MAX_SLOTS];
static uint32_t s_slot_count;

/**
 * @brief 清理区间内的旧 schedule 槽位内容。
 *
 * 仅用于回滚时清除被截断的尾部槽位，避免调试输出里出现脏数据。
 */
static void sched_clear_range(uint32_t from, uint32_t to) {
    uint32_t i;

    if (from >= to || from >= BM_CONFIG_MP_SCHEDULE_MAX_SLOTS) {
        return;
    }
    if (to > BM_CONFIG_MP_SCHEDULE_MAX_SLOTS) {
        to = BM_CONFIG_MP_SCHEDULE_MAX_SLOTS;
    }
    for (i = from; i < to; i++) {
        memset(&s_slots[i], 0, sizeof(s_slots[i]));
    }
}

void bm_mp_schedule_reset(void) {
    memset(s_slots, 0, sizeof(s_slots));
    s_slot_count = 0u;
}

uint32_t bm_mp_schedule_mark(void) {
    return s_slot_count;
}

void bm_mp_schedule_restore(uint32_t mark) {
    if (mark > BM_CONFIG_MP_SCHEDULE_MAX_SLOTS) {
        mark = BM_CONFIG_MP_SCHEDULE_MAX_SLOTS;
    }
    if (mark < s_slot_count) {
        sched_clear_range(mark, s_slot_count);
    }
    s_slot_count = mark;
}

int bm_mp_schedule_register(const bm_mp_schedule_slot_t *slot) {
    if (bm_mp_profile_is_built()) {
        return BM_ERR_BUSY;
    }
    if (!slot || !slot->name || slot->owner_cpu >= BM_CONFIG_CPU_COUNT) {
        return BM_ERR_INVALID;
    }
#if BM_CONFIG_MP_HARD_RT_PROFILE && BM_CONFIG_MP_PROFILE_STREAM_GATE_ENFORCED
    if ((slot->flags & BM_MP_SCHEDULE_FLAG_STREAM) != 0u) {
        return BM_ERR_INVALID;
    }
#endif
    if (s_slot_count >= BM_CONFIG_MP_SCHEDULE_MAX_SLOTS) {
        return BM_ERR_NO_MEM;
    }
    s_slots[s_slot_count++] = *slot;
    return BM_OK;
}

static uint64_t sched_slot_execution_us(const bm_mp_schedule_slot_t *s) {
    return (uint64_t)s->wcet_us +
           (uint64_t)s->stream_scan_us +
           (uint64_t)s->stream_commit_us +
           (uint64_t)s->relay_copy_us +
           (uint64_t)s->cache_maint_us;
}

static int sched_rta_response_us(uint8_t cpu, uint32_t slot_index,
                                 uint64_t *response_out) {
    const bm_mp_schedule_slot_t *s = &s_slots[slot_index];
    uint64_t r;
    uint32_t iter;
    uint32_t j;

    r = sched_slot_execution_us(s) +
        (uint64_t)s->blocking_us +
        (uint64_t)s->bus_interference_us;

    for (iter = 0u; iter < BM_CONFIG_MP_RTA_MAX_ITERATIONS; iter++) {
        uint64_t r_next = sched_slot_execution_us(s) +
                          (uint64_t)s->blocking_us +
                          (uint64_t)s->bus_interference_us;

        for (j = 0u; j < s_slot_count; j++) {
            const bm_mp_schedule_slot_t *hp = &s_slots[j];
            uint64_t interfere;

            if (hp->owner_cpu != cpu || j == slot_index ||
                hp->period_us == 0u) {
                continue;
            }
            if (hp->deadline_us > s->deadline_us ||
                (hp->deadline_us == s->deadline_us && j > slot_index)) {
                continue;
            }
            /*
             * 标准 RTA 固定点迭代：高优先级干扰基于上一轮响应时间 r，
             * 而非本轮累加值 r_next。r 为上一轮完整结果，保证收敛到
             * 正确的最大响应时间。
             */
            {
                uint64_t jobs =
                    (r + (uint64_t)hp->period_us - 1u) /
                    (uint64_t)hp->period_us;
                uint64_t hp_exec = sched_slot_execution_us(hp);

                if (hp_exec != 0u && jobs > UINT64_MAX / hp_exec) {
                    return BM_ERR_OVERFLOW;
                }
                interfere = jobs * hp_exec;
            }
            if (r_next > UINT64_MAX - interfere) {
                return BM_ERR_OVERFLOW;
            }
            r_next += interfere;
        }
        if (r_next == r) {
            *response_out = r;
            return BM_OK;
        }
        if (r_next > (uint64_t)s->deadline_us) {
            *response_out = r_next;
            return BM_OK;
        }
        r = r_next;
    }
    return BM_ERR_TIMEOUT;
}

static int validate_cpu(uint8_t cpu, bm_mp_schedule_cpu_report_t *report_out) {
    uint64_t util_ppm = 0u;
    uint32_t worst_r = 0u;
    uint32_t cpu_slot_count = 0u;
    uint32_t i;

    for (i = 0u; i < s_slot_count; i++) {
        const bm_mp_schedule_slot_t *s = &s_slots[i];
        uint64_t response_us;
        uint64_t execution_us;

        if (s->owner_cpu != cpu) {
            continue;
        }
        int rta_rc;

        if (s->deadline_us == 0u || s->period_us == 0u ||
            s->wcet_us == 0u) {
            return BM_ERR_INVALID;
        }
        if ((s->flags & BM_MP_SCHEDULE_FLAG_STREAM) != 0u &&
            (s->stream_scan_us == 0u || s->stream_commit_us == 0u ||
             s->cache_maint_us == 0u)) {
            return BM_ERR_INVALID;
        }
        if ((s->flags & BM_MP_SCHEDULE_FLAG_RELAY) != 0u &&
            (s->relay_copy_us == 0u || s->cache_maint_us == 0u)) {
            return BM_ERR_INVALID;
        }
        if (s->wcet_us > s->deadline_us) {
            return BM_ERR_INVALID;
        }
        rta_rc = sched_rta_response_us(cpu, i, &response_us);
        if (rta_rc != BM_OK) {
            return rta_rc;
        }
        execution_us = sched_slot_execution_us(s);
        if (response_us > s->deadline_us ||
            response_us > UINT32_MAX) {
            return BM_ERR_OVERFLOW;
        }
        if ((uint32_t)response_us > worst_r) {
            worst_r = (uint32_t)response_us;
        }
        cpu_slot_count++;
        if (s->period_us > 0u) {
            util_ppm += (execution_us * 1000000ull) /
                        (uint64_t)s->period_us;
        }
    }

    if (report_out) {
        report_out->cpu = cpu;
        report_out->slot_count = cpu_slot_count;
        report_out->utilization_ppm = (uint32_t)(util_ppm > 1000000ull ?
                                                 1000000ull : util_ppm);
        report_out->worst_response_us = worst_r;
        report_out->passed = (util_ppm <= 1000000ull) ? 1 : 0;
    }

    if (util_ppm > 1000000ull) {
        BM_LOGE("mp_sched", "cpu %u util overflow ppm=%u",
                (unsigned)cpu, (unsigned)util_ppm);
        return BM_ERR_OVERFLOW;
    }
#if BM_CONFIG_MP_HARD_RT_PROFILE
    if (cpu_slot_count == 0u) {
        BM_LOGE("mp_sched", "cpu %u has no schedule slots", (unsigned)cpu);
        return BM_ERR_INVALID;
    }
#endif
    return BM_OK;
}

int bm_mp_partition_validate_schedule(uint8_t cpu,
                                      bm_mp_schedule_cpu_report_t *report_out) {
    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return BM_ERR_INVALID;
    }
    return validate_cpu(cpu, report_out);
}

void bm_mp_schedule_print_report(void) {
    uint8_t cpu;

    BM_LOGI("mp_sched", "=== schedule report cpus=%u slots=%u ===",
            (unsigned)BM_CONFIG_CPU_COUNT, (unsigned)s_slot_count);
    for (cpu = 0u; cpu < BM_CONFIG_CPU_COUNT; cpu++) {
        bm_mp_schedule_cpu_report_t rep = {0};
        int rc = validate_cpu(cpu, &rep);

        if (rc != BM_OK) {
            BM_LOGI("mp_sched",
                    "cpu=%u util=%u.%02u%% worst_R=%uus rc=%d",
                    (unsigned)cpu,
                    (unsigned)(rep.utilization_ppm / 10000u),
                    (unsigned)((rep.utilization_ppm % 10000u) / 100u),
                    (unsigned)rep.worst_response_us, rc);
            continue;
        }
        BM_LOGI("mp_sched",
                "cpu=%u util=%u.%02u%% worst_R=%uus rc=%d",
                (unsigned)rep.cpu,
                (unsigned)(rep.utilization_ppm / 10000u),
                (unsigned)((rep.utilization_ppm % 10000u) / 100u),
                (unsigned)rep.worst_response_us, rc);
    }
}

int bm_mp_schedule_register_main_loop_overhead(uint8_t cpu) {
    bm_mp_schedule_slot_t slot;
    uint64_t wcet_us;
    uint64_t ipc_msgs;

    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return BM_ERR_INVALID;
    }

    wcet_us = BM_CONFIG_MP_STREAM_DRAIN_BUDGET *
              BM_CONFIG_MP_STREAM_DRAIN_WCET_PER_BLOCK_US;
#if BM_MP_MULTICORE
    ipc_msgs = (BM_CONFIG_CPU_COUNT - 1u) *
               BM_CONFIG_MP_IPC_PER_SOURCE_BUDGET;
#else
    ipc_msgs = 0u;
#endif
    wcet_us += ipc_msgs * BM_CONFIG_MP_IPC_DRAIN_WCET_PER_MSG_US;
    wcet_us += BM_CONFIG_MP_RELAY_DRAIN_BUDGET *
               BM_CONFIG_MP_RELAY_DRAIN_WCET_PER_SLOT_US;
    wcet_us += BM_CONFIG_MP_EVENT_PROCESS_BUDGET *
               BM_CONFIG_MP_IPC_DRAIN_WCET_PER_MSG_US;
    wcet_us += BM_CONFIG_MP_MAIN_LOOP_FIXED_OVERHEAD_US;
    wcet_us += BM_CONFIG_MP_STREAM_DRAIN_BUDGET *
               BM_CONFIG_MP_STREAM_ACCOUNT_WCET_PER_BLOCK_US;
    /* Bootstrap 是所有 per-CPU 日志环的唯一消费者。 */
    if (cpu == BM_CONFIG_MP_BOOTSTRAP_CPU) {
        wcet_us += (uint64_t)BM_CONFIG_CPU_COUNT *
                   (uint64_t)BM_CONFIG_MP_LOG_DRAIN_BUDGET *
                   (uint64_t)BM_CONFIG_MP_LOG_DRAIN_WCET_PER_MSG_US;
    }
    if (wcet_us == 0u || wcet_us > UINT32_MAX) {
        return BM_ERR_OVERFLOW;
    }

    memset(&slot, 0, sizeof(slot));
    slot.name = "mp_main_loop";
    slot.owner_cpu = cpu;
    slot.wcet_us = (uint32_t)wcet_us;
    slot.period_us = BM_CONFIG_MP_MAIN_LOOP_PERIOD_US;
    slot.deadline_us = BM_CONFIG_MP_MAIN_LOOP_DEADLINE_US;
    return bm_mp_schedule_register(&slot);
}
