/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_tt_schedule.c
 * @brief 时间触发调度门面（bm_tt_schedule）实现
 *
 * 本轮（Task 5）把 tick 长成最终形态——双域派发器：ISR 域新增 reentry
 * guard（`rt->running` 已为真则本拍整体跳过、只计 overrun，不 freeze/不
 * step/不 publish）；MAINLOOP 域新增真正的冻结挂起/发布调度（`fresh` 驱动
 * 发布上一拍主循环算完的结果，`pending` 驱动本拍是否需要新冻结或计
 * overrun），ISR 内绝不对 MAINLOOP 域跑 step；`bm_tt_schedule_run_pending`
 * 从占位桩实现为主循环侧有界 drain（跑纯 step、翻 phase、置 fresh、清
 * pending，但不发布，发布交回下一次 tick）；`bm_tt_schedule_hrt_slot` 从
 * 占位桩实现为真正的 slot 描述符（回调经 `tt_hrt_trampoline` 转
 * `bm_tt_schedule_tick`）。其余算法（init 校验、RTA 导出）留待后续 Task。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-07-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-01       1.0            zeh            骨架发布（config+公共头+CMake，无算法实现）
 * 2026-07-01       1.1            zeh            Task 3：输入冻结 + seq-delta 判龄，最小 ISR tick
 * 2026-07-01       1.2            zeh            Task 4：per-task 相位双缓冲 + 边界发布，扩展 ISR tick
 * 2026-07-01       1.3            zeh            Task 5：双域派发器（ISR reentry/overrun +
 *                                                 MAINLOOP fresh/pending）+ run_pending + hrt slot
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
 * @brief hrt ISR 蹦床：仅转调派发器，供 `bm_tt_schedule_hrt_slot` 的
 * `callback` 字段引用（`bm_hrt_callback_t` 签名为 `void(*)(void*)`，与
 * `bm_tt_schedule_tick(bm_tt_schedule_t*)` 不同，需一层转调）
 *
 * @param ctx 实为 `bm_tt_schedule_t*`（hrt slot 的 context 字段回传）
 */
static void tt_hrt_trampoline(void *ctx) {
    bm_tt_schedule_tick((bm_tt_schedule_t *)ctx);
}

/**
 * @brief 生成本调度表对应的 HRT slot 描述
 *
 * @details period_us=minor_us，trigger=TIMER，callback 经 `tt_hrt_trampoline`
 * 转调本调度表的 `bm_tt_schedule_tick`，context=本调度表实例。
 *
 * @param sched 调度表实例
 * @return HRT slot 描述符
 */
bm_hrt_slot_t bm_tt_schedule_hrt_slot(bm_tt_schedule_t *sched) {
    bm_hrt_slot_t slot = { sched->minor_us, BM_HRT_TRIGGER_TIMER,
                            tt_hrt_trampoline, sched, sched->name };
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
 * @brief 双域派发器：ISR 域同步跑完 step，MAINLOOP 域只冻结/发布、挂起给
 * `bm_tt_schedule_run_pending` 执行
 *
 * @details 遍历 entries，`(tick_idx % every)==at` 命中本拍才处理该 activity：
 *
 * - **ISR 域**：reentry guard 优先——`rt->running` 已为真说明上一拍还没跑完
 *   （正常情况下不会发生，出现即视为 overrun），本拍整体跳过（不 freeze/
 *   不 step/不 publish），仅 `overrun_count+1`；否则 `running=1` →
 *   `tt_freeze_inputs` → `step`（写 `outbuf[phase]`）→ `tt_publish`（发布
 *   `outbuf[phase^1]`，即上一次已完成的结果）→ `rt->phase^=1`（翻转，供
 *   下次命中写入另一份缓冲）→ `running=0`。
 * - **MAINLOOP 域**：① 若 `rt->fresh` 为真，说明主循环已在上一次
 *   `run_pending` 里跑完 step，本拍先 `tt_publish` 发布该结果、再清
 *   `fresh`；② 之后若 `rt->pending` 为真，说明上一拍冻结的输入主循环还没
 *   消化，`overrun_count+1`（丢本拍，**不**重新冻结、不覆盖已冻结的输入
 *   快照）；否则 `tt_freeze_inputs` + 置 `pending=1`，等主循环
 *   `run_pending` 消化。**ISR 里绝不对 MAINLOOP 域任务调用 step**。
 *
 * 单核下 ISR 抢占主循环、读改 `fresh`/`pending` 存在良性竞态窗口：最坏
 * 情形是偶发多计一次 overrun 或结果晚发一拍，无数据损坏、有界，故不加
 * 临界区/锁（spec 决策，见 `bm_tt_schedule_run_pending` 注释）。
 *
 * 处理完全部 activity 后 `tick_idx` 按 `n_frames` 取模前进一拍。
 *
 * @param sched 调度表实例
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
        if ((sched->tick_idx % a->every) != a->at) {
            continue; /* 未命中本拍 */
        }
        if (a->domain == BM_TT_DOMAIN_ISR) {
            if (a->rt->running) {
                a->rt->overrun_count += 1u; /* 上拍未完 → 本拍整体跳过，不发布 */
                continue;
            }
            {
                bm_let_ctx_t ctx = { .sched = sched, .act = a };

                a->rt->running = 1u;
                tt_freeze_inputs(sched, a);
                a->step(&ctx, a->state);
                tt_publish(sched, a);
                a->rt->phase ^= 1u;
                a->rt->running = 0u;
            }
        } else { /* BM_TT_DOMAIN_MAINLOOP */
            if (a->rt->fresh) {
                tt_publish(sched, a); /* 发布主循环刚完成的结果 */
                a->rt->fresh = 0u;
            }
            if (a->rt->pending) {
                a->rt->overrun_count += 1u; /* 主循环还没消化上一拍 → 丢、不重冻结 */
            } else {
                tt_freeze_inputs(sched, a);
                a->rt->pending = 1u;
            }
        }
    }
    sched->tick_idx = (n > 0u) ? ((sched->tick_idx + 1u) % n) : 0u;
}

/**
 * @brief 主循环侧有界 drain：跑 MAINLOOP 域 pending 任务的纯 step
 *
 * @details 遍历 entries 中 `domain==MAINLOOP` 且 `rt->pending` 为真的任务，
 * 按 `ran < budget` 有界执行：`step`（写 `outbuf[phase]`）→
 * `rt->phase^=1`（完成结果落到 phase^1，待下次 tick 发）→ `rt->fresh=1`
 * （告知下次 tick 有新结果待发）→ `rt->pending=0`（消化完毕）。
 *
 * @note 本函数**不发布**——发布交回下一次 tick 里 ISR 侧 MAINLOOP 分支看到
 * `fresh` 后做，与本函数解耦。单核下 ISR 抢占主循环存在良性竞态窗口
 * （`fresh`/`pending` 读改）：最坏情形是 overrun 偶发多计一次或结果晚发
 * 一拍，无数据损坏、有界；L1/L2 视 overrun 为提示量，不需要精确即可
 * 接受，故不加 `bm_critical_wrap` 或锁（spec 决策）。需要精确统计时才
 * 需要临界区包住 fresh/pending 的读改。
 *
 * @param sched 调度表实例
 * @param budget 本次最多运行的任务数
 * @return 本次实际运行的任务数
 */
uint32_t bm_tt_schedule_run_pending(bm_tt_schedule_t *sched, uint32_t budget) {
    uint32_t ran = 0u;

    if (sched == NULL) {
        return 0u;
    }
    for (uint8_t i = 0u; i < sched->entry_count && ran < budget; ++i) {
        bm_tt_activity_t *a = sched->entries[i];
        bm_let_ctx_t ctx = { .sched = sched, .act = a };

        if (a->domain != BM_TT_DOMAIN_MAINLOOP) {
            continue;
        }
        if (!a->rt->pending) {
            continue;
        }
        a->step(&ctx, a->state); /* 重计算在主循环跑 */
        a->rt->phase ^= 1u;      /* 完成结果落 phase^1，待下一次 tick 发布 */
        a->rt->fresh = 1u;
        a->rt->pending = 0u;
        ran++;
    }
    return ran;
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
