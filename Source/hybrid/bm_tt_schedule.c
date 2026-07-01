/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_tt_schedule.c
 * @brief 时间触发调度门面（bm_tt_schedule）实现
 *
 * 本轮（Task 4）在 Task 3 输入冻结基础上补齐 LET 输出通路：per-task 相位
 * 双缓冲 + 边界发布——`bm_let_out` 返回 `outbuf[phase]` 中 out_idx 对应槽的
 * 写指针；`tt_publish` 把上一次已完成的结果（`outbuf[phase^1]`）经
 * `tt_bus_publish`（acquire_write→memcpy→commit）逐个发布到各输出 bus；
 * `bm_tt_schedule_tick` ISR 分支扩展为 freeze→step→publish→phase 翻转。
 * 其余算法（init 校验、MAINLOOP pending 执行、RTA 导出、reentry/overrun）
 * 留待后续 Task 填充。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-01       1.0            zeh            骨架发布（config+公共头+CMake，无算法实现）
 * 2026-07-01       1.1            zeh            Task 3：输入冻结 + seq-delta 判龄，最小 ISR tick
 * 2026-07-01       1.2            zeh            Task 4：per-task 相位双缓冲 + 边界发布，扩展 ISR tick
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
    uint32_t off = 0u;

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
        off += in->elem_size;
    }
}

/**
 * @brief step 内读取输入快照
 */
const void *bm_let_in(bm_let_ctx_t *ctx, uint32_t in_idx, int *out_stale,
                       uint32_t *out_age_us) {
    const bm_tt_activity_t *a = ctx->act;
    uint32_t off = 0u;

    for (uint32_t i = 0u; i < in_idx; ++i) {
        off += a->inputs[i].elem_size;
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
 * @brief LATEST 发布助手：acquire_write → memcpy → commit（bus 无单调用 write）
 * @details commit 内部完成 seqlock +2（奇→偶）。非阻塞、有界；失败返回码上抛。
 * @param bus 目标 LATEST bus
 * @param src 待发布数据起始地址
 * @param sz 待发布字节数
 * @return BM_OK 成功；其他为错误码
 */
static int tt_bus_publish(bm_bus_t *bus, const void *src, uint32_t sz) {
    void *slot;
    int rc = bm_bus_acquire_write(bus, &slot);

    if (rc != BM_OK) {
        return rc;
    }
    (void)memcpy(slot, src, sz);
    return bm_bus_commit(bus);
}

/**
 * @brief step 内获取输出写入缓冲
 *
 * @details 返回 `outbuf` 中 `rt->phase` 对应那一份缓冲里、`out_idx` 对应
 * 输出槽的写指针。stride = Σ outputs[].elem_size；offset = out_idx 之前
 * 所有输出的 elem_size 累加。全程用 uint32_t/size_t，不做 uint8_t 窄化
 * 累加（避免 elem_size 之和越界回绕）。
 *
 * @param ctx step 上下文
 * @param out_idx 输出索引（对应绑定表下标）
 * @return 输出数据写指针
 */
void *bm_let_out(bm_let_ctx_t *ctx, uint32_t out_idx) {
    bm_tt_activity_t *a = ctx->act;
    uint32_t stride = 0u;
    uint32_t off = 0u;

    for (uint8_t i = 0u; i < a->output_count; ++i) {
        stride += a->outputs[i].elem_size;
    }
    for (uint32_t i = 0u; i < out_idx; ++i) {
        off += a->outputs[i].elem_size;
    }
    return (uint8_t *)a->outbuf + (size_t)a->rt->phase * stride + off;
}

/**
 * @brief 边界发布：把上一次已完成的结果（outbuf[phase^1]）发到各输出对应的 bus
 *
 * @details 本次命中 step 刚写完 `outbuf[phase]`；`phase^1` 那份缓冲是上一次
 * 命中已完成的结果，正是本次该对外发布的值（+1 任务周期延迟）。**不翻转
 * phase**——翻转时机 ISR/MAINLOOP 不同（ISR：发布后翻；MAINLOOP：step 完成时
 * 翻），故与发布解耦，交调用方按域择时处理。overrun/skip 不发布，留待 Task5。
 *
 * @param s 调度表实例（本任务未使用，保留供后续扩展）
 * @param a 目标 activity（取 outputs/outbuf/rt）
 */
static void tt_publish(bm_tt_schedule_t *s, bm_tt_activity_t *a) {
    uint32_t stride = 0u;
    uint32_t off = 0u;
    uint8_t prev;

    (void)s;
    for (uint8_t i = 0u; i < a->output_count; ++i) {
        stride += a->outputs[i].elem_size;
    }
    prev = a->rt->phase ^ 1u;
    for (uint8_t i = 0u; i < a->output_count; ++i) {
        const uint8_t *src = (const uint8_t *)a->outbuf + (size_t)prev * stride + off;

        (void)tt_bus_publish(a->outputs[i].bus, src, a->outputs[i].elem_size); /* seq +2 由 commit 保证 */
        off += a->outputs[i].elem_size;
    }
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
 * @brief ISR 派发器：freeze→step→publish→phase 翻转（Task 4）
 *
 * @details 遍历 entries，`(tick_idx % every)==at` 命中的 ISR 域 activity
 * 依次执行 `tt_freeze_inputs` → `step`（写入 `outbuf[phase]`）→
 * `tt_publish`（发布 `outbuf[phase^1]`，即上一次已完成的结果）→
 * `rt->phase ^= 1u`（翻转，供下次命中写入另一份缓冲），随后 `tick_idx`
 * 按 `n_frames` 取模前进一拍。reentry/overrun/MAINLOOP/run_pending 见 Task5。
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
                tt_publish(sched, a);
                a->rt->phase ^= 1u;
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
