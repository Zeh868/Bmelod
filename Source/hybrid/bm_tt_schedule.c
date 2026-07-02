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
 * `bm_tt_schedule_tick`）。本轮（Task 6）把 `bm_tt_schedule_init` 从占位桩
 * 落成真实现：参数/周期一致性校验 → N=LCM(every) 且 ≤ MAX_FRAMES → 节拍
 * 负载校验（`tt_frame_check`，快路 Σwcet 直过/慢路逐格 AP-fill 累加）→
 * 每 output 双缓冲两半预填 safe_default 并发布到 bus（首拍前下游即可读到
 * 安全值）→ 每 input 快照 baseline_seq → 复位 rt。Task 3 加的临时兜底
 * `tt_calc_frames_fallback` 一并删除，tick 改为直接信任 init 已算好的
 * `n_frames`。本轮（Task 7）把 RTA 中立描述符导出 + 调度概览报告从占位桩
 * 落成真实现：`bm_tt_schedule_rt_slot_count/at` 只读遍历 entries 数
 * ISR 域 activity、按 idx 定位并填充中立描述符（`owner_cpu` 恒 0，本轮
 * 单核视角）；`bm_tt_schedule_report` 用栈上定长行缓冲
 * （`snprintf` 有界格式化，零动态分配）逐行经 `emit` 回调发出两块内容：
 * ISR 域·时间格视图（表头标注"声明 wcet_us·计划视图"时间来源 + 逐 minor
 * 格 Σ 命中 activity wcet_us 找峰值格并核对 `≤ minor_us`）与 MAINLOOP
 * 域·预算账（无硬时间格语义，逐行列 wcet_us + 建议 run_pending budget）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.5
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
 * 2026-07-01       1.4            zeh            Task 6：真 init（周期一致性 + N=LCM + 节拍负载校验 +
 *                                                 预发布 safe_default + baseline_seq），删临时兜底
 * 2026-07-01       1.5            zeh            Task 7：RTA rt_slot 导出 + 调度概览 report
 *                                                 （ISR 时间格视图 + MAINLOOP 预算账）真实现
 *
 */
#include "bm_tt_schedule.h"
#include "bm_config.h"
#include "bm_log.h"
#include "bm_safety.h"

#include <stdio.h>
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
 * 记新基线；`age = miss × 任务周期`（以 uint64_t 中间量计算并钳制到
 * `UINT32_MAX`，防止超长断流后 uint32_t 乘法回绕假性清除 stale），
 * `stale = (max_age_us!=0 && age>max_age_us)`。一旦 age 已饱和到
 * `UINT32_MAX`，后续不再自增 miss（防止 miss 自身回绕），从而保持
 * "曾经饱和过就恒为 stale" 的 fail-safe 语义。`max_age_us==BM_LET_AGE_DEFAULT`
 * 时解析为 2×任务周期。拷出失败（尚未发布或 seqlock 重试耗尽）时填充
 * `safe_default` 并强制 stale=1，miss 保持不变。
 *
 * @param s 调度表实例（取 minor_us 算任务周期）
 * @param a 目标 activity（取 inputs/snapshot/rt）
 */
/** LET 输入龄的饱和值（uint32_t 上限 UINT32_MAX，"曾饱和恒 stale" 语义） */
#define BM_LET_AGE_SATURATED 0xFFFFFFFFu

static void tt_freeze_inputs(bm_tt_schedule_t *s, bm_tt_activity_t *a) {
    uint32_t period_us = s->minor_us * a->every;
    uint32_t off = 0u;
    /*
     * 饱和阈值：miss 增至该值后 age = miss×period 便达到 UINT32_MAX，此后停增
     * 以防 miss 自身回绕（P1-5）。用 miss 与阈值比较判定饱和，替代旧实现复用
     * age_us==UINT32_MAX 哨兵——读失败路径也写该哨兵，会与"真饱和"混淆，导致
     * 下一拍读成功但 seq 未变时误挡 miss 自增、age 少计一拍。period_us==0 时
     * age 恒 0 不会饱和，阈值取 UINT32_MAX 仅防 miss 回绕。
     */
    uint32_t miss_saturated = (period_us == 0u) ? BM_LET_AGE_SATURATED
                                                : (BM_LET_AGE_SATURATED / period_us);

    for (uint8_t i = 0u; i < a->input_count; ++i) {
        const bm_let_input_t *in = &a->inputs[i];
        uint8_t *snap = (uint8_t *)a->snapshot + off;
        uint32_t seq = 0u;
        int rc = bm_bus_latest_read_seq(in->bus, snap, &seq);
        uint32_t max_age = (in->max_age_us == BM_LET_AGE_DEFAULT)
                                ? (BM_LET_AGE_DEFAULT_PERIODS * period_us)
                                : in->max_age_us;

        if (rc != BM_OK) {
            (void)memcpy(snap, in->safe_default, in->elem_size);
            a->rt->stale[i] = 1;
            a->rt->age_us[i] = BM_LET_AGE_SATURATED; /* miss 保持不变 */
        } else {
            if (seq != a->rt->baseline_seq[i]) {
                a->rt->miss[i] = 0u;
                a->rt->baseline_seq[i] = seq;
            } else if (a->rt->miss[i] < miss_saturated) {
                /* 尚未饱和才继续自增 miss，避免超长断流后 miss 自身回绕 */
                a->rt->miss[i] += 1u;
            }
            {
                uint64_t age64 = (uint64_t)a->rt->miss[i] * (uint64_t)period_us;
                uint32_t age = (age64 > BM_LET_AGE_SATURATED)
                                   ? BM_LET_AGE_SATURATED
                                   : (uint32_t)age64;

                a->rt->age_us[i] = age;
                a->rt->stale[i] = (max_age != 0u && age > max_age) ? 1 : 0;
            }
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
 * 翻），故与发布解耦，交调用方按域择时处理。overrun/skip 分支（见
 * `bm_tt_schedule_tick`）由调用方直接跳过、不调用本函数，故天然不发布。
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
 * @brief 计算最大公约数（辗转相除）
 * @param a 输入 a
 * @param b 输入 b
 * @return gcd(a, b)
 */
static uint32_t tt_gcd(uint32_t a, uint32_t b) {
    while (b != 0u) {
        uint32_t t = a % b;

        a = b;
        b = t;
    }
    return a;
}

/**
 * @brief 计算最小公倍数
 * @details 先除后乘，避免 a*b 中间结果在 uint32_t 上溢出。
 * @param a 输入 a
 * @param b 输入 b
 * @return lcm(a, b)
 */
static uint32_t tt_lcm(uint32_t a, uint32_t b) {
    return a / tt_gcd(a, b) * b;
}

/**
 * @brief 节拍负载校验（两层）
 *
 * @details 快路：若全体 ISR 域任务 `wcet_us` 之和 ≤ `minor_us`，则无论如何
 * 错峰同一 minor 格内都不可能超载，直接判过、不必逐格展开（多任务表常见
 * 场景，避免不必要的 O(n_frames) 遍历）。慢路：快路不满足时才逐格精算——
 * 按 AP-fill（活动周期展开）把每个 ISR 域任务的命中拍 `t = at, at+every,
 * at+2*every, ...` 都累加 `wcet_us` 进 `w[t]`，任一格 `w[t] > minor_us` 即
 * 该格实际重叠超载，返回 `BM_ERR_INVALID`。`w[]` 用 static 数组（非栈上
 * 大数组），大小为编译期上界 `BM_CONFIG_TT_SCHED_MAX_FRAMES`，`n_frames`
 * 已在调用前由 init 校验 ≤ 该上界。
 *
 * @param s 调度表实例（只读 entries/minor_us）
 * @param n_frames 本表的 N=LCM(every)，已由调用方校验 ≤ MAX_FRAMES
 * @return BM_OK 可调度；BM_ERR_INVALID 某格超载
 * @note 非可重入/不可并发调用（单核串行 init 期专用）
 */
static int tt_frame_check(const bm_tt_schedule_t *s, uint32_t n_frames) {
    uint32_t sum = 0u;
    static uint32_t w[BM_CONFIG_TT_SCHED_MAX_FRAMES];

    for (uint8_t k = 0u; k < s->entry_count; ++k) {
        if (s->entries[k]->domain == BM_TT_DOMAIN_ISR) {
            /* 饱和加：裸 u32 加法回绕会让超载和绕回小值假性通过快路（P1-6） */
            sum = bm_u32_saturating_add(sum, s->entries[k]->wcet_us);
        }
    }
    if (sum <= s->minor_us) {
        return BM_OK; /* 快路：整体 wcet 之和已不超，任一格必不超 */
    }
    for (uint32_t t = 0u; t < n_frames; ++t) {
        w[t] = 0u;
    }
    for (uint8_t k = 0u; k < s->entry_count; ++k) {
        const bm_tt_activity_t *a = s->entries[k];

        if (a->domain != BM_TT_DOMAIN_ISR) {
            continue;
        }
        for (uint32_t t = a->at; t < n_frames; t += a->every) {
            w[t] += a->wcet_us;
            if (w[t] > s->minor_us) {
                return BM_ERR_INVALID;
            }
        }
    }
    return BM_OK;
}

/**
 * @brief 初始化调度表：校验 + 算 N=LCM + 节拍负载校验 + 预发布 safe_default
 *
 * @details 依次：① 参数/周期一致性校验（`minor_us`、`entry_count`、每任务
 * `every/at`、`input_count`、每 input/output 的 `elem_size` 上界、每 output
 * 的 `safe_default` 非空）→ ② `n = LCM(every)`，超过 `BM_CONFIG_TT_SCHED_
 * MAX_FRAMES` 即拒（挡 LCM 爆炸）→ ③ `tt_frame_check` 节拍负载校验 → ④ 每
 * output 的双缓冲两份都预填 `safe_default` 并经 `tt_bus_publish` 发布到
 * bus（使首拍 tick 之前下游即可读到安全值）→ ⑤ 每 input 用
 * `bm_bus_latest_read_seq` 快照 `baseline_seq`（读不到则置 0）、`miss`
 * 清零 → ⑥ 复位 rt（`phase/running/pending/fresh/overrun_count` 清零）→
 * 设 `n_frames = n`、`tick_idx = 0`。
 *
 * @param sched 调度表实例
 * @return BM_OK 成功；BM_ERR_INVALID 参数/周期不一致、LCM 超界或节拍超载
 */
int bm_tt_schedule_init(bm_tt_schedule_t *sched) {
    uint32_t n;
    int rc;

    if (sched == NULL || sched->minor_us == 0u || sched->entry_count == 0u ||
        sched->entry_count > BM_CONFIG_TT_SCHED_MAX_ENTRIES) {
        return BM_ERR_INVALID;
    }

    n = 1u;
    for (uint8_t k = 0u; k < sched->entry_count; ++k) {
        bm_tt_activity_t *a = sched->entries[k];

        if (a->every == 0u || a->at >= a->every) {
            return BM_ERR_INVALID;
        }
        if (a->input_count > BM_CONFIG_TT_SCHED_MAX_INPUTS) {
            return BM_ERR_INVALID;
        }
        for (uint8_t i = 0u; i < a->input_count; ++i) {
            if (a->inputs[i].elem_size > BM_CONFIG_TT_SCHED_MAX_ELEM_SIZE) {
                return BM_ERR_INVALID;
            }
        }
        for (uint8_t o = 0u; o < a->output_count; ++o) {
            if (a->outputs[o].elem_size > BM_CONFIG_TT_SCHED_MAX_ELEM_SIZE) {
                return BM_ERR_INVALID;
            }
            if (a->outputs[o].safe_default == NULL) {
                return BM_ERR_INVALID;
            }
        }
        n = tt_lcm(n, a->every);
        if (n > BM_CONFIG_TT_SCHED_MAX_FRAMES) {
            return BM_ERR_INVALID; /* 挡 LCM 爆炸 */
        }
    }

    rc = tt_frame_check(sched, n);
    if (rc != BM_OK) {
        return rc;
    }

    for (uint8_t k = 0u; k < sched->entry_count; ++k) {
        bm_tt_activity_t *a = sched->entries[k];
        uint32_t stride = 0u;
        uint32_t off = 0u;

        for (uint8_t o = 0u; o < a->output_count; ++o) {
            stride += a->outputs[o].elem_size;
        }
        for (uint8_t o = 0u; o < a->output_count; ++o) {
            for (uint8_t ph = 0u; ph < 2u; ++ph) {
                uint8_t *dst = (uint8_t *)a->outbuf + (size_t)ph * stride + off;

                (void)memcpy(dst, a->outputs[o].safe_default, a->outputs[o].elem_size);
            }
            (void)tt_bus_publish(a->outputs[o].bus, a->outputs[o].safe_default,
                                  a->outputs[o].elem_size);
            off += a->outputs[o].elem_size;
        }

        for (uint8_t i = 0u; i < a->input_count; ++i) {
            uint8_t tmp[BM_CONFIG_TT_SCHED_MAX_ELEM_SIZE];
            uint32_t seq = 0u;

            if (bm_bus_latest_read_seq(a->inputs[i].bus, tmp, &seq) == BM_OK) {
                a->rt->baseline_seq[i] = seq;
            } else {
                a->rt->baseline_seq[i] = 0u;
            }
            a->rt->miss[i] = 0u;
        }

        a->rt->phase = 0u;
        a->rt->running = 0u;
        a->rt->pending = 0u;
        a->rt->fresh = 0u;
        a->rt->overrun_count = 0u;
    }

    sched->n_frames = n;
    sched->tick_idx = 0u;
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
    n = sched->n_frames; /* 由 bm_tt_schedule_init 算好；未 init 直接 tick 属误用 */

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

/** report 框架开销占位（Task 7）：本格 Σ step wcet 之外，ISR 派发/上下文
 *  切换等框架自身开销尚无实测数据，先占位为 0，待后续 Task 用真机实测
 *  校准后替换为非零常量或可配置项。 */
#define TT_REPORT_OVERHEAD_US_PLACEHOLDER 0u

/** report 逐行栈缓冲上界（字节）。上界估算见 bm_tt_schedule_report 注释：
 *  activity 名 63B + 中文表头 ~100B + 数个 uint32_t 十进制字段 + 分隔符，留余量。 */
#define TT_REPORT_LINE_MAX 200

/**
 * @brief report 用：统计某个 minor 格内所有命中 ISR 域 activity 的 wcet_us 之和
 *
 * @param s 调度表实例（只读 entries）
 * @param t 目标 minor 格（0..n_frames-1）
 * @return Σ 命中该格的 ISR 域 activity wcet_us（不含框架开销占位）
 */
static uint32_t tt_report_frame_sum_us(const bm_tt_schedule_t *s, uint32_t t) {
    uint32_t sum = 0u;

    for (uint8_t k = 0u; k < s->entry_count; ++k) {
        const bm_tt_activity_t *a = s->entries[k];

        if (a->domain != BM_TT_DOMAIN_ISR || a->every == 0u) {
            continue;
        }
        if ((t % a->every) == a->at) {
            sum += a->wcet_us;
        }
    }
    return sum;
}

/**
 * @brief 输出调度表可读诊断报告：ISR 域时间格视图 + MAINLOOP 域预算账
 *
 * @details 用栈上定长行缓冲 `char line[TT_REPORT_LINE_MAX]` 逐行 `snprintf`
 * 有界格式化后经 `emit(line, u)` 发出，全程零动态分配。上界估算：activity
 * 名字按合理上限 63 字节 + 固定中文表头（含"[时间来源: 声明 wcet_us ·
 * 计划视图]"等，UTF-8 下约 90～100 字节）+ 数个 uint32_t 十进制字段
 * （每个至多 10 位）+ 分隔符，留有充分余量；`snprintf` 返回值不做特殊
 * 处理——越界只会被截断、不会溢出缓冲区。
 *
 * 块①：ISR 域·时间格视图。表头固定含子串
 * "[时间来源: 声明 wcet_us · 计划视图]"（标注这是基于任务声明 wcet_us
 * 的静态计划推演，非真机实测）；每个 ISR 域 activity 一行列
 * name/every/at/wcet_us；随后经 `tt_report_frame_sum_us` 扫描
 * `sched->n_frames` 个 minor 格（叠加框架开销占位
 * `TT_REPORT_OVERHEAD_US_PLACEHOLDER`，当前为 0，待后续 Task 实测校准）
 * 找出峰值格，输出该格是否 `≤ minor_us`。
 *
 * 块②：MAINLOOP 域·预算账。MAINLOOP 域无硬时间格语义，不做展开——逐行
 * 列每个 MAINLOOP 域 activity 的 `wcet_us` 与建议 `run_pending` budget
 * （固定给 1，供开发者对照真机主循环率手工核对，非精确算法）。
 *
 * @param sched 调度表实例（只读）
 * @param emit 逐行输出回调
 * @param u emit 回调透传上下文
 */
void bm_tt_schedule_report(const bm_tt_schedule_t *sched,
                           void (*emit)(const char *line, void *u), void *u) {
    char line[TT_REPORT_LINE_MAX];
    uint32_t peak_t = 0u;
    uint32_t peak_us = 0u;

    if (sched == NULL || emit == NULL) {
        return;
    }

    (void)snprintf(line, sizeof line,
                   "=== %s ISR 域·时间格视图 [时间来源: 声明 wcet_us · 计划视图] ===",
                   sched->name);
    emit(line, u);

    for (uint8_t k = 0u; k < sched->entry_count; ++k) {
        const bm_tt_activity_t *a = sched->entries[k];

        if (a->domain != BM_TT_DOMAIN_ISR) {
            continue;
        }
        (void)snprintf(line, sizeof line,
                       "  ISR name=%s every=%u at=%u wcet_us=%u",
                       a->name, (unsigned)a->every, (unsigned)a->at, a->wcet_us);
        emit(line, u);
    }

    for (uint32_t t = 0u; t < sched->n_frames; ++t) {
        uint32_t cur = tt_report_frame_sum_us(sched, t) + TT_REPORT_OVERHEAD_US_PLACEHOLDER;

        if (cur > peak_us) {
            peak_us = cur;
            peak_t = t;
        }
    }
    (void)snprintf(line, sizeof line,
                   "  峰值格: t=%u, 本格us=%u, \xe2\x89\xa4minor_us(%u) %s",
                   peak_t, peak_us, sched->minor_us,
                   (peak_us <= sched->minor_us) ? "\xe2\x9c\x93" : "\xe2\x9c\x97");
    emit(line, u);

    (void)snprintf(line, sizeof line, "=== %s MAINLOOP 域·预算账 ===", sched->name);
    emit(line, u);

    for (uint8_t k = 0u; k < sched->entry_count; ++k) {
        const bm_tt_activity_t *a = sched->entries[k];

        if (a->domain != BM_TT_DOMAIN_MAINLOOP) {
            continue;
        }
        (void)snprintf(line, sizeof line,
                       "  MAINLOOP name=%s wcet_us=%u run_pending_budget_hint=1",
                       a->name, a->wcet_us);
        emit(line, u);
    }
}

/**
 * @brief 查询调度表可导出的 RTA slot 数量
 *
 * @details 数 entries 中 `domain==BM_TT_DOMAIN_ISR` 的 activity 个数——
 * RTA（响应时间分析）只对 ISR 域任务有意义，MAINLOOP 域无硬时间格语义、
 * 不纳入。
 *
 * @param sched 调度表实例（只读）
 * @return slot 数量
 */
uint32_t bm_tt_schedule_rt_slot_count(const bm_tt_schedule_t *sched) {
    uint32_t c = 0u;

    if (sched == NULL) {
        return 0u;
    }
    for (uint8_t k = 0u; k < sched->entry_count; ++k) {
        if (sched->entries[k]->domain == BM_TT_DOMAIN_ISR) {
            c++;
        }
    }
    return c;
}

/**
 * @brief 按索引导出 RTA 中立只读描述符
 *
 * @details 按 idx 定位第 idx 个 ISR 域 activity（跳过 MAINLOOP 域），
 * 填充 `owner_cpu=0`（本轮单核视角）、`kind`/`domain` 原样转存、
 * `wcet_us` 原样转存、`period_us = minor_us × every`、
 * `deadline_us = period_us`（本轮 deadline 恒等于周期，未来若要支持
 * 隐式 deadline 之外的显式 deadline 需扩展 activity 字段）。idx 越界
 * （超出 ISR 域 activity 数量）返回 `BM_ERR_INVALID`，越界前不写 `*out`。
 *
 * @param sched 调度表实例（只读）
 * @param idx slot 索引
 * @param out 输出描述符
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效或索引越界
 */
int bm_tt_schedule_rt_slot_at(const bm_tt_schedule_t *sched, uint32_t idx,
                              bm_tt_schedule_rt_slot_t *out) {
    uint32_t c = 0u;

    if (sched == NULL || out == NULL) {
        return BM_ERR_INVALID;
    }
    for (uint8_t k = 0u; k < sched->entry_count; ++k) {
        const bm_tt_activity_t *a = sched->entries[k];

        if (a->domain != BM_TT_DOMAIN_ISR) {
            continue;
        }
        if (c == idx) {
            uint32_t period = sched->minor_us * a->every;

            out->owner_cpu = 0u;
            out->kind = (uint8_t)a->kind;
            out->domain = (uint8_t)a->domain;
            out->wcet_us = a->wcet_us;
            out->period_us = period;
            out->deadline_us = period;
            return BM_OK;
        }
        c++;
    }
    return BM_ERR_INVALID;
}
