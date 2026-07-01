/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_tt_schedule.c
 * @brief 时间触发调度门面（bm_tt_schedule）实现
 *
 * 本轮（Task 3）落地第一块真引擎：输入冻结 + seq-delta 判龄——
 * `tt_freeze_inputs` 在每拍命中时对 activity 的每个输入做一次 LATEST
 * 拷出式读（`bm_bus_latest_read_seq`），以稳定 seq 是否变化推算 miss/age/
 * stale；`bm_let_in` 供 step 函数读取冻结后的快照与新鲜度。`bm_tt_schedule_tick`
 * 当前仅为最小 ISR 派发（见函数内注释），其余算法（init 校验、MAINLOOP
 * pending 执行、RTA 导出）留待后续 Task 填充。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-01       1.0            zeh            骨架发布（config+公共头+CMake，无算法实现）
 * 2026-07-01       1.1            zeh            Task 3：输入冻结 + seq-delta 判龄，最小 ISR tick
 *
 */
#include "bm_tt_schedule.h"
#include "bm_config.h"
#include "bm_log.h"

#include <string.h>

/**
 * @brief step 上下文实体定义（公共头仅暴露不透明 typedef，仅本引擎可见）
 */
struct bm_let_ctx {
    bm_tt_schedule_t *sched; /**< 所属调度表 */
    bm_tt_activity_t *act;   /**< 当前正在执行的 activity */
};

/**
 * @brief 周期头对某 activity 的每个输入做一次冻结 + seq-delta 判龄
 *
 * @details 拷出路径经 `bm_bus_latest_read_seq`（LATEST 多观察者拷出 +
 * 回传稳定 seq）；seq 相较上次冻结基线未变则 miss 计数递增，变了则清零并
 * 记新基线；`age = miss × 任务周期`，`stale = (max_age_us!=0 && age>max_age_us)`。
 * `max_age_us==BM_LET_AGE_DEFAULT` 时解析为 2×任务周期。拷出失败（尚未发布
 * 或 seqlock 重试耗尽）时填充 `safe_default` 并强制 stale=1，miss 保持不变。
 *
 * @param s 调度表实例（取 minor_us 算任务周期）
 * @param a 目标 activity（取 inputs/snapshot/rt）
 */
static void tt_freeze_inputs(bm_tt_schedule_t *s, bm_tt_activity_t *a) {
    uint32_t period_us = s->minor_us * a->every;
    uint8_t off = 0u;

    for (uint8_t i = 0u; i < a->input_count; ++i) {
        const bm_let_input_t *in = &a->inputs[i];
        uint8_t *snap = (uint8_t *)a->snapshot + off;
        uint32_t seq = 0u;
        int rc = bm_bus_latest_read_seq(in->bus, snap, &seq);
        uint32_t max_age = (in->max_age_us == BM_LET_AGE_DEFAULT)
                                ? (2u * period_us)
                                : in->max_age_us;

        if (rc != BM_OK) {
            (void)memcpy(snap, in->safe_default, in->elem_size);
            a->rt->stale[i] = 1;
            a->rt->age_us[i] = 0xFFFFFFFFu; /* miss 保持不变 */
        } else {
            if (seq != a->rt->baseline_seq[i]) {
                a->rt->miss[i] = 0u;
                a->rt->baseline_seq[i] = seq;
            } else {
                a->rt->miss[i] += 1u;
            }
            uint32_t age = a->rt->miss[i] * period_us;
            a->rt->age_us[i] = age;
            a->rt->stale[i] = (max_age != 0u && age > max_age) ? 1 : 0;
        }
        off += (uint8_t)in->elem_size;
    }
}

/**
 * @brief step 内读取输入快照
 */
const void *bm_let_in(bm_let_ctx_t *ctx, uint32_t in_idx, int *out_stale,
                       uint32_t *out_age_us) {
    const bm_tt_activity_t *a = ctx->act;
    uint8_t off = 0u;

    for (uint32_t i = 0u; i < in_idx; ++i) {
        off += (uint8_t)a->inputs[i].elem_size;
    }
    if (out_stale) {
        *out_stale = a->rt->stale[in_idx];
    }
    if (out_age_us) {
        *out_age_us = a->rt->age_us[in_idx];
    }
    return (const uint8_t *)a->snapshot + off;
}

/**
 * @brief step 内获取输出写入缓冲（占位桩）
 */
void *bm_let_out(bm_let_ctx_t *ctx, uint32_t out_idx) {
    (void)ctx;
    (void)out_idx;
    return NULL;
}

/**
 * @brief 初始化调度表（占位桩，暂不校验/分配布局）
 */
int bm_tt_schedule_init(bm_tt_schedule_t *sched) {
    (void)sched;
    return BM_OK;
}

/**
 * @brief 生成本调度表对应的 HRT slot 描述（占位桩，空 slot）
 */
bm_hrt_slot_t bm_tt_schedule_hrt_slot(bm_tt_schedule_t *sched) {
    bm_hrt_slot_t slot;

    (void)sched;
    memset(&slot, 0, sizeof(slot));
    return slot;
}

/**
 * @brief 计算 entries[].every 的最小公倍数（tick_idx 归零边界的兜底）
 *
 * @details 正式 init（Task 5）会在初始化期算好 `sched->n_frames`；本任务
 * 未实现 init，故 tick 首次调用时若发现 `n_frames==0` 用此兜底就地补算，
 * 仅保证 tick_idx 归零边界不越界，不做任何绑定表校验。
 *
 * @param s 调度表实例（只读 entries/every）
 * @return every 的 LCM；entry_count==0 时返回 1
 */
static uint32_t tt_calc_frames_fallback(const bm_tt_schedule_t *s) {
    uint32_t n = 1u;

    for (uint8_t i = 0u; i < s->entry_count; ++i) {
        uint32_t e = s->entries[i]->every;
        uint32_t a;
        uint32_t b;
        uint32_t g;

        if (e == 0u) {
            continue;
        }
        a = n;
        b = e;
        while (b != 0u) {
            uint32_t t = b;
            b = a % b;
            a = t;
        }
        g = a; /* gcd(n, e) */
        n = (n / g) * e;
    }
    return n;
}

/**
 * @brief ISR 派发器：最小版实现（Task 3）
 *
 * @details 遍历 entries，`(tick_idx % every)==at` 命中的 ISR 域 activity
 * 依次执行 `tt_freeze_inputs` → `step`，随后 `tick_idx` 按 `n_frames` 取模
 * 前进一拍。最小版：仅 freeze→step(ISR)。publish/phase 见 Task4；
 * reentry/overrun/MAINLOOP/run_pending 见 Task5。
 */
void bm_tt_schedule_tick(bm_tt_schedule_t *sched) {
    uint32_t n;

    if (sched == NULL) {
        return;
    }
    if (sched->n_frames == 0u) {
        sched->n_frames = tt_calc_frames_fallback(sched);
    }
    n = sched->n_frames;

    for (uint8_t i = 0u; i < sched->entry_count; ++i) {
        bm_tt_activity_t *a = sched->entries[i];

        if (a->every == 0u) {
            continue;
        }
        if ((sched->tick_idx % a->every) == a->at) {
            if (a->domain == BM_TT_DOMAIN_ISR) {
                bm_let_ctx_t ctx = { .sched = sched, .act = a };

                tt_freeze_inputs(sched, a);
                a->step(&ctx, a->state);
            }
            /* MAINLOOP 域：冻结后置 pending，交 run_pending 执行——留待 Task5 */
        }
    }
    sched->tick_idx = (n > 0u) ? ((sched->tick_idx + 1u) % n) : 0u;
}

/**
 * @brief 主循环调用：跑 MAINLOOP 域 pending step（占位桩，暂不运行）
 */
uint32_t bm_tt_schedule_run_pending(bm_tt_schedule_t *sched, uint32_t budget) {
    (void)sched;
    (void)budget;
    return 0u;
}

/**
 * @brief 输出调度表可读诊断报告（占位桩，暂不输出）
 */
void bm_tt_schedule_report(const bm_tt_schedule_t *sched,
                           void (*emit)(const char *line, void *u), void *u) {
    (void)sched;
    (void)emit;
    (void)u;
}

/**
 * @brief 查询调度表可导出的 RTA slot 数量（占位桩，恒为 0）
 */
uint32_t bm_tt_schedule_rt_slot_count(const bm_tt_schedule_t *sched) {
    (void)sched;
    return 0u;
}

/**
 * @brief 按索引导出 RTA 中立只读描述符（占位桩，恒返回 BM_ERR_INVALID）
 */
int bm_tt_schedule_rt_slot_at(const bm_tt_schedule_t *sched, uint32_t idx,
                              bm_tt_schedule_rt_slot_t *out) {
    (void)sched;
    (void)idx;
    (void)out;
    return BM_ERR_INVALID;
}
