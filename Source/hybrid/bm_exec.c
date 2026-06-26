/**
 * @file bm_exec.c
 * @brief 确定性执行实例的批量生命周期管理实现
 *
 * 校验实例与资源声明，组装 HRT 调度表，协调 init/start/stop 与硬件绑定。
 * @author zeh (china_qzh@163.com)
 * @version 2.5
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-11       1.1            zeh            SIL-2 会话状态与 start 失败回滚
 * 2026-06-11       1.2            zeh            start_all 先启实例后启 HRT；NULL 前置校验
 * 2026-06-11       1.3            zeh            Periodic 槽会话守卫与 slot_count 上界
 * 2026-06-12       2.0            zeh            直接迁移为领域中性 bm_exec
 * 2026-06-12       2.1            zeh            Block/Frame 槽与 bm_stream 绑定
 * 2026-06-13       2.2            zeh            Block 槽 deadline 检查与 late 统计
 * 2026-06-14       2.3            zeh            按 CPU 会话；stream commit/drain 解耦
 * 2026-06-14       2.4            zeh            deadline miss 可注册处理；按 CPU clock_id
 * 2026-06-26       2.5            zeh            deadline 时间基迁至 bm_uptime_us()（#9-2a）
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "bm_exec.h"
#include "bm_stream.h"
#include "bm/hybrid/bm_timestamp.h"
#include "bm/common/bm_uptime.h"
#include "bm_critical_wrap.h"
#include "bm_hrt.h"
#include "bm_log.h"
#include "hal/bm_hal_timer.h"
#include "hal/bm_hal_cpu.h"
#include "hal/bm_hal_critical.h"
#include "bm/common/bm_atomic_ipc.h"

#include "bm/core/bm_cpu_local.h"

#include <string.h>

/** 实例槽位绑定：用于 HRT/硬件回调上下文 */
typedef struct {
    const bm_exec_t *instance;
    const bm_exec_slot_t *slot;
} bm_exec_binding_t;

/** 按 CPU 维护的 exec 会话运行时状态（每个逻辑 CPU 一份） */
typedef struct {
    const bm_exec_t     *instances[BM_CONFIG_MAX_EXEC_INSTANCES];
    uint32_t             instance_count;
    bm_exec_binding_t    bindings[BM_CONFIG_MAX_EXEC_SLOTS];
    uint32_t             binding_count;
    bm_hrt_slot_t        hrt_slots[BM_CONFIG_HRT_MAX_SLOTS];
    uint32_t             hrt_slot_count;
    uint32_t             init_done_count;
    bm_atomic_ipc_u32_t session;
} bm_exec_cpu_state_t;

typedef struct {
    bm_exec_cpu_state_t state;
    uint8_t padding[BM_CONFIG_CACHE_LINE -
                    (sizeof(bm_exec_cpu_state_t) % BM_CONFIG_CACHE_LINE)];
} bm_exec_cpu_storage_t;

static BM_CACHE_ALIGNAS(BM_CONFIG_CACHE_LINE)
bm_exec_cpu_storage_t g_exec_cpu[BM_CONFIG_CPU_COUNT];
static bm_exec_irq_release_gate_t s_irq_release_gate;
static bm_exec_deadline_miss_fn_t s_deadline_miss_handler;

static bm_exec_cpu_state_t *bm_exec_this(void) {
    uint32_t cpu = bm_hal_cpu_id();
    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return NULL;
    }
    return &g_exec_cpu[cpu].state;
}

#define g_instances       (bm_exec_this()->instances)
#define g_instance_count  (bm_exec_this()->instance_count)
#define g_bindings        (bm_exec_this()->bindings)
#define g_binding_count   (bm_exec_this()->binding_count)
#define g_hrt_slots       (bm_exec_this()->hrt_slots)
#define g_hrt_slot_count  (bm_exec_this()->hrt_slot_count)
#define g_init_done_count (bm_exec_this()->init_done_count)

/**
 * @brief 设置 IRQ 释放门控回调
 *
 * 在 bm_exec_irq_release_all 调用前检查是否允许释放 IRQ。
 *
 * @param gate 门控回调函数指针
 */
void bm_exec_set_irq_release_gate(bm_exec_irq_release_gate_t gate) {
    s_irq_release_gate = gate;
}

/**
 * @brief 设置 deadline 错过处理函数
 *
 * @param fn 处理函数指针；为 NULL 时使用默认弱钩子
 */
void bm_exec_set_deadline_miss_handler(bm_exec_deadline_miss_fn_t fn) {
    s_deadline_miss_handler = fn;
}

/**
 * @brief 检查当前 CPU 的 exec 状态是否有效
 */
static int exec_cpu_valid(void) {
    return bm_exec_this() != NULL;
}

/**
 * @brief Block/Frame 槽错过 deadline 时的弱钩子（默认空实现）
 *
 * 不支持弱符号的平台可定义 `BM_CONFIG_EXEC_EXTERNAL_DEADLINE_HOOK=1`
 * 并由应用提供钩子实现。
 */
#if !defined(BM_CONFIG_EXEC_EXTERNAL_DEADLINE_HOOK) || \
    !(BM_CONFIG_EXEC_EXTERNAL_DEADLINE_HOOK)
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void bm_exec_block_deadline_missed_hook(const bm_exec_slot_t *slot,
                                        bm_block_t *block,
                                        uint32_t elapsed_us) {
    (void)slot;
    (void)block;
    (void)elapsed_us;
}
#endif

/**
 * @brief 检查 Block/Frame 槽是否已超 deadline
 *
 * 时间基已迁至 bm_uptime_us()（64 位单调微秒，#9-2a）：
 *  - now_us = bm_uptime_us()（64 位，无回绕）
 *  - block_ts_us 由 block->timestamp 通用公式 ticks * 1e6 / rate_hz 换算
 *  - elapsed64 = now_us - block_ts_us（直接 64 位减法，无截断）
 *
 * 跨时钟域处理（clock_id/clock_epoch/rate_hz 重对齐）保持原有逻辑：
 * HRT 同域时校验并修正 epoch/rate，跨域时信任调用方时间戳。
 *
 * @param slot        执行槽描述符指针
 * @param block       数据块指针
 * @param elapsed_out 输出实际 elapsed 微秒（uint32_t，超出 4G 时饱和为 0xFFFFFFFF）
 * @return 1 已超 deadline；0 未超或无法判断
 */
static int exec_block_is_late(const bm_exec_slot_t *slot,
                              bm_block_t *block,
                              uint32_t *elapsed_out) {
    uint64_t now_us;
    uint64_t block_ts_us;
    uint64_t elapsed64;
    uint32_t timer_freq;

    if (elapsed_out != NULL) {
        *elapsed_out = 0u;
    }
    if (slot == NULL || block == NULL || slot->deadline_us == 0u) {
        return 0;
    }
    if (block->timestamp.rate_hz == 0u) {
        return 0;
    }
    /*
     * timer_freq 用于 HRT 同域时的 clock_epoch/rate_hz 重对齐；
     * 若定时器尚未初始化（freq=0），重对齐会将 rate_hz 置 0 导致除零，
     * 提前返回以保障安全。
     */
    timer_freq = bm_hal_timer_get_freq();
    if (timer_freq == 0u) {
        return 0;
    }
    {
        bm_hal_timer_handle_t timer = bm_hal_timer_for_cpu(bm_hal_cpu_id());

        /*
         * 确定性流式按 CPU 路由：
         * block 携带的时钟域信息（clock_id/clock_epoch）与本地 clock_id
         * 不一致时，说明数据已在上层完成转送并重新采样。
         *
         * clock_id 匹配 → 进行 epoch/rate 重对齐（见下）。
         * clock_id 不匹配 → 跨时钟域，信任调用方时间戳，直接进入检查路径。
         */
        if (block->timestamp.clock_id == timer.clock_id) {
            /*
             * profile build 会 bump 全核 clock_epoch；块若在 bump 前后沿采样，
             * epoch/rate 可能与消费侧不一致，按当前时钟域重对齐后再算 elapsed。
             */
            if (block->timestamp.clock_epoch != timer.clock_epoch ||
                block->timestamp.rate_hz != timer_freq) {
                block->timestamp.clock_epoch = timer.clock_epoch;
                block->timestamp.rate_hz = timer_freq;
            }
        }
        /* 跨时钟域：跳过 clock_id/epoch 校验，信任调用方的时间戳 */
    }
    /* 统一单调时钟：64 位微秒，无回绕 */
    now_us      = bm_uptime_us();
    block_ts_us = block->timestamp.ticks * 1000000ull /
                  (uint64_t)block->timestamp.rate_hz;
    if (now_us < block_ts_us) {
        /* block 时间戳在未来，尚未到期 */
        return 0;
    }
    elapsed64 = now_us - block_ts_us;
    if (elapsed64 > (uint64_t)slot->deadline_us) {
        /* elapsed 饱和截断至 uint32_t，供上层钩子展示 */
        uint32_t elapsed_u32 = (elapsed64 > 0xFFFFFFFFull)
                               ? 0xFFFFFFFFu : (uint32_t)elapsed64;
        if (slot->stream != NULL) {
            bm_stream_mark_late(slot->stream);
        }
        if (elapsed_out != NULL) {
            *elapsed_out = elapsed_u32;
        }
        return 1;
    }
    return 0;
}

/**
 * @brief 执行 Block/Frame 槽：检查 deadline 并调用 run_block
 *
 * @param instance 执行实例指针
 * @param slot 槽描述符指针
 * @param block 数据块指针
 */
static void exec_run_block_slot(const bm_exec_t *instance,
                                const bm_exec_slot_t *slot,
                                bm_block_t *block) {
    uint32_t elapsed_us = 0u;

    if (instance == NULL || slot == NULL || block == NULL ||
        slot->run_block == NULL) {
        return;
    }
    if (exec_block_is_late(slot, block, &elapsed_us)) {
        if (s_deadline_miss_handler) {
            s_deadline_miss_handler(slot, block, elapsed_us);
        } else {
            bm_exec_block_deadline_missed_hook(slot, block, elapsed_us);
        }
        (void)bm_stream_consumer_release(slot->stream, block);
        return;
    }
    slot->run_block(instance, block);
    if ((slot->flags & BM_EXEC_SLOT_FLAG_FRAMEWORK_RELEASE) != 0u) {
        (void)bm_stream_consumer_release(slot->stream, block);
    }
}

/**
 * @brief 获取当前 exec 会话状态（原子加载）
 */
static inline bm_exec_session_t exec_get_session(const bm_exec_cpu_state_t *s) {
    return (bm_exec_session_t)bm_atomic_ipc_load_u32(&s->session);
}

/**
 * @brief 设置当前 exec 会话状态（原子存储）
 */
static void exec_set_session(bm_exec_session_t v) {
    bm_exec_cpu_state_t *s = bm_exec_this();
    bm_irq_state_t irq_state = BM_CRITICAL_ENTER();

    bm_atomic_ipc_store_u32(&s->session, (uint32_t)v);
    BM_CRITICAL_EXIT(irq_state);
}

/**
 * @brief HRT/硬件回调入口，执行绑定槽位的 run 函数
 *
 * @param context 指向 bm_exec_binding_t 的上下文指针
 */
static void bm_exec_run_binding(void *context) {
    const bm_exec_binding_t *binding = (const bm_exec_binding_t *)context;
    bm_exec_cpu_state_t *state = bm_exec_this();

    if (!state || !binding || !binding->slot || !binding->slot->run || !binding->instance) {
        return;
    }
        /* 原子加载保证状态可见性；ISR 热路径直接使用本地状态指针 */
    if (exec_get_session(state) != BM_EXEC_SESSION_STARTED) {
        return;
    }
    binding->slot->run(binding->instance);
}

/**
 * @brief 清零运行时全局状态（实例表、绑定表、HRT 槽）
 */
/**
 * @brief 清零运行时全局状态（实例表、绑定表、HRT 槽）
 */
static void exec_clear_runtime(void) {
    if (!exec_cpu_valid()) {
        return;
    }
    {
        uint32_t n;
        for (n = 0u; n < BM_CONFIG_MAX_EXEC_INSTANCES; ++n) {
            g_instances[n] = NULL;
        }
    }
    g_instance_count = 0u;
    memset(g_bindings, 0, sizeof(g_bindings));
    g_binding_count = 0u;
    memset(g_hrt_slots, 0, sizeof(g_hrt_slots));
    g_hrt_slot_count = 0u;
    g_init_done_count = 0u;
    exec_set_session(BM_EXEC_SESSION_NONE);
}

/*
 * 确定性流式 drain 扫描：
 *
 * 运行时使用 pending_drain 标志避免空闲扫描，但该标志仅对本地
 * stream 可见。
 *
 * drain 统一走此函数，通过主循环按预算轮询。
 * 空闲时须遍历全部 stream 槽以检查新到达的 block。
 * 此扫描开销已计入 bm_mp_schedule_register_main_loop_overhead() 的
 * WCET 预算中，使 WCET 可静态分析。
 */
/**
 * @brief 按预算轮询所有 Block/Frame 槽的 stream，消费 READY 块
 *
 * @param budget 最大消费块数
 * @return 实际消费块数
 */
static int exec_drain_stream_slots(uint32_t budget) {
    uint32_t i;
    uint32_t s;
    uint32_t drained = 0u;

    for (i = 0u; i < g_instance_count && drained < budget; ++i) {
        const bm_exec_t *inst = g_instances[i];
        for (s = 0u; s < inst->slot_count && drained < budget; ++s) {
            const bm_exec_slot_t *slot = &inst->slots[s];

            if ((slot->kind != BM_EXEC_SLOT_BLOCK &&
                 slot->kind != BM_EXEC_SLOT_FRAME) ||
                !slot->stream || !slot->run_block) {
                continue;
            }

            while (drained < budget) {
                bm_block_t *block;
                if (bm_stream_consumer_acquire(slot->stream, &block) != BM_OK) {
                    break;
                }
                exec_run_block_slot(inst, slot, block);
                drained++;
                if (exec_get_session(bm_exec_this()) != BM_EXEC_SESSION_STARTED) {
                    return (int)drained;
                }
            }
        }
    }
    return (int)drained;
}

/**
 * @brief 公开 API：按预算 drain 当前核的 Block/Frame stream
 *
 * @param budget 最大消费块数
 * @return 实际消费块数；未启动或在 ISR 中返回 0
 */
int bm_exec_drain_streams(uint32_t budget) {
    if (bm_hal_in_isr()) {
        return 0;
    }
    if (exec_get_session(bm_exec_this()) != BM_EXEC_SESSION_STARTED) {
        return 0;
    }
    return exec_drain_stream_slots(budget);
}

/**
 * @brief 解绑所有 stream 槽的 ready handler
 */
static void exec_unbind_stream_slots(void) {
    uint32_t i;
    uint32_t s;

    for (i = 0u; i < g_instance_count; ++i) {
        const bm_exec_t *inst = g_instances[i];
        for (s = 0u; s < inst->slot_count; ++s) {
            const bm_exec_slot_t *slot = &inst->slots[s];
            if (slot->kind == BM_EXEC_SLOT_BLOCK ||
                slot->kind == BM_EXEC_SLOT_FRAME) {
                if (slot->stream) {
                    bm_stream_set_ready_handler(slot->stream, NULL, NULL);
                }
            }
        }
    }
}

/**
 * @brief 解绑所有硬件槽位的外部中断/定时器
 */
static void exec_unbind_all_hardware(void) {
    uint32_t i;
    uint32_t s;

    exec_unbind_stream_slots();
    for (i = 0u; i < g_instance_count; ++i) {
        const bm_exec_t *inst = g_instances[i];
        for (s = 0u; s < inst->slot_count; ++s) {
            const bm_exec_slot_t *slot = &inst->slots[s];
            if (slot->kind == BM_EXEC_SLOT_HARDWARE && slot->bind) {
                (void)slot->bind(inst, NULL);
            }
        }
    }
}

/**
 * @brief 停止 HRT、安全停机、解绑硬件并清空运行时状态
 */
static void exec_teardown_session(void) {
    uint32_t i;

    exec_set_session(BM_EXEC_SESSION_STOPPING);
    bm_hrt_stop();
    exec_unbind_all_hardware();

    if (g_instance_count > 0u) {
        for (i = g_instance_count; i > 0u; --i) {
            const bm_exec_t *inst = g_instances[i - 1u];
            if (inst && inst->ops && inst->ops->safe_stop) {
                inst->ops->safe_stop(inst);
            }
        }
    }

    bm_hrt_reset();
    exec_clear_runtime();
}

/**
 * @brief 按逆序回滚已完成的实例 init，调用 safe_stop
 */
static void exec_rollback_inits(void) {
    while (g_init_done_count > 0u) {
        g_init_done_count--;
        const bm_exec_t *inst = g_instances[g_init_done_count];
        if (inst->ops && inst->ops->safe_stop) {
            inst->ops->safe_stop(inst);
        }
    }
}

/**
 * @brief start/init 失败回滚：安全停机全部已 init 实例并释放资源
 */
static void exec_abort_session(void) {
    exec_set_session(BM_EXEC_SESSION_STOPPING);
    bm_hrt_stop();
    exec_unbind_all_hardware();
    exec_rollback_inits();
    bm_hrt_reset();
    exec_clear_runtime();
}

/**
 * @brief 校验单个执行实例描述符与槽位配置
 *
 * @param inst 执行实例指针
 * @return BM_OK 有效；BM_ERR_INVALID 字段或槽位配置无效
 */
static int validate_instance(const bm_exec_t *inst) {
    uint32_t s;

    if (!inst || !inst->ops) {
        return BM_ERR_INVALID;
    }
    if (inst->slot_count > 0u && !inst->slots) {
        return BM_ERR_INVALID;
    }
    if (inst->slot_count == 0u && inst->slots != NULL) {
        return BM_ERR_INVALID;
    }
    if (!inst->ops->init || !inst->ops->start || !inst->ops->safe_stop) {
        return BM_ERR_INVALID;
    }
    if (inst->claim_count > 0u && !inst->claims) {
        return BM_ERR_INVALID;
    }
    if (inst->slot_count > BM_CONFIG_MAX_EXEC_SLOTS) {
        return BM_ERR_INVALID;
    }

    for (s = 0u; s < inst->slot_count; ++s) {
        const bm_exec_slot_t *slot = &inst->slots[s];
        if (slot->kind == BM_EXEC_SLOT_PERIODIC) {
            if (slot->bind != NULL) {
                return BM_ERR_INVALID;
            }
            if (!slot->run) {
                return BM_ERR_INVALID;
            }
            if (bm_hrt_validate_period_us(slot->period_us) != BM_OK) {
                return BM_ERR_INVALID;
            }
        } else if (slot->kind == BM_EXEC_SLOT_HARDWARE) {
            if (!slot->bind || !slot->run) {
                return BM_ERR_INVALID;
            }
            if (slot->period_us != 0u || slot->deadline_us != 0u || slot->stream) {
                return BM_ERR_INVALID;
            }
        } else if (slot->kind == BM_EXEC_SLOT_BLOCK ||
                   slot->kind == BM_EXEC_SLOT_FRAME) {
            if (!slot->run_block || !slot->stream) {
                return BM_ERR_INVALID;
            }
            if (slot->deadline_us == 0u) {
                return BM_ERR_INVALID;
            }
#if BM_CPU_LOCAL_ENABLE_ROUTE
            if ((slot->flags & BM_EXEC_SLOT_FLAG_FRAMEWORK_RELEASE) == 0u) {
                return BM_ERR_INVALID;
            }
#endif
            if (slot->bind != NULL || slot->run != NULL) {
                return BM_ERR_INVALID;
            }
            /*
             * 确定性流式双消费者互斥（全剖面，含单核）：绑定到 exec block/frame
             * 槽的 stream 由 bm_exec_drain_streams 经 consumer_acquire 消费；若该
             * stream 同时挂了 on_ready 回调，单核 bm_stream_drain 会与
             * exec_drain_streams 并发争用同一 READY 队列，形成双消费者竞争
             *（违反 SPSC）。故 exec 验证期统一拒绝该组合。
             */
            if (slot->stream->on_ready != NULL) {
                return BM_ERR_INVALID;
            }
        } else {
            return BM_ERR_INVALID;
        }
    }

    return BM_OK;
}

static int validate_stream_owner(void) {
    uint32_t i;
    uint32_t s;

    /*
     * 该校验在实例 init op 之后运行（on_ready 通常在 init 期经
     * bm_stream_set_ready_handler / bm_stream_init 之前配置），故能可靠拦截
     * 「绑定 exec block/frame 槽 + on_ready 回调」的双消费者组合（契约 3）。
     */
    for (i = 0u; i < g_instance_count; ++i) {
        const bm_exec_t *inst = g_instances[i];
        for (s = 0u; s < inst->slot_count; ++s) {
            const bm_exec_slot_t *slot = &inst->slots[s];
            if ((slot->kind == BM_EXEC_SLOT_BLOCK ||
                 slot->kind == BM_EXEC_SLOT_FRAME) &&
                slot->stream != NULL) {
                if (slot->stream->owner_cpu != inst->owner_cpu) {
                    return BM_ERR_INVALID;
                }
                /*
                 * 确定性流式双消费者互斥：exec block/frame 槽由
                 * bm_exec_drain_streams 消费；该 stream 不得同时挂 on_ready
                 * 回调（单核 bm_stream_drain 会并发争用 READY 队列）。
                 */
                if (slot->stream->on_ready != NULL) {
                    return BM_ERR_INVALID;
                }
            }
        }
    }
    return BM_OK;
}

/**
 * @brief 校验实例 ID 在批次内唯一
 *
 * @param instances 实例指针数组
 * @param count 实例数量
 * @return BM_OK 唯一；BM_ERR_ALREADY 存在重复 ID
 */
static int validate_unique_ids(const bm_exec_t *const *instances,
                               uint32_t count) {
    uint32_t i;
    uint32_t j;

    for (i = 0u; i < count; ++i) {
        if (!instances[i]) {
            return BM_ERR_INVALID;
        }
    }
    for (i = 0u; i < count; ++i) {
        for (j = i + 1u; j < count; ++j) {
            if (instances[i]->id == instances[j]->id) {
                return BM_ERR_ALREADY;
            }
        }
    }
    return BM_OK;
}

/**
 * @brief 组装绑定表与 HRT 调度槽表
 *
 * @param instances 实例指针数组
 * @param count 实例数量
 * @return BM_OK 成功；BM_ERR_OVERFLOW 槽位表溢出
 */
static int assemble_tables(const bm_exec_t *const *instances,
                           uint32_t count) {
    uint32_t i;
    uint32_t s;

    g_binding_count = 0u;
    g_hrt_slot_count = 0u;

    for (i = 0u; i < count; ++i) {
        const bm_exec_t *inst = instances[i];
        for (s = 0u; s < inst->slot_count; ++s) {
            const bm_exec_slot_t *slot = &inst->slots[s];

            if (g_binding_count >= BM_CONFIG_MAX_EXEC_SLOTS) {
                return BM_ERR_OVERFLOW;
            }
            if (slot->kind == BM_EXEC_SLOT_BLOCK ||
                slot->kind == BM_EXEC_SLOT_FRAME) {
                uint32_t b;
                for (b = 0u; b < g_binding_count; ++b) {
                    const bm_exec_slot_t *bound_slot = g_bindings[b].slot;
                    if ((bound_slot->kind == BM_EXEC_SLOT_BLOCK ||
                         bound_slot->kind == BM_EXEC_SLOT_FRAME) &&
                        bound_slot->stream == slot->stream) {
                        return BM_ERR_INVALID;
                    }
                }
            }
            g_bindings[g_binding_count].instance = inst;
            g_bindings[g_binding_count].slot = slot;
            g_binding_count++;

            if (slot->kind != BM_EXEC_SLOT_PERIODIC) {
                continue;
            }
            if (g_hrt_slot_count >= BM_CONFIG_HRT_MAX_SLOTS) {
                return BM_ERR_OVERFLOW;
            }
            g_hrt_slots[g_hrt_slot_count].period_us = slot->period_us;
            g_hrt_slots[g_hrt_slot_count].trigger = BM_HRT_TRIGGER_TIMER;
            g_hrt_slots[g_hrt_slot_count].callback = bm_exec_run_binding;
            g_hrt_slots[g_hrt_slot_count].context =
                &g_bindings[g_binding_count - 1u];
            g_hrt_slots[g_hrt_slot_count].name = slot->name;
            g_hrt_slot_count++;
        }
    }

    return BM_OK;
}

static int validate_instances_on_this_cpu(const bm_exec_t *const *instances,
                                          uint32_t count) {
    uint32_t cpu = BM_CPU_THIS();
    uint32_t i;

    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return BM_ERR_INVALID;
    }
    for (i = 0u; i < count; ++i) {
        if (!instances[i]) {
            return BM_ERR_INVALID;
        }
        if (instances[i]->owner_cpu != (uint8_t)cpu) {
            BM_LOGE("exec", "owner mismatch inst=%s cpu=%u owner=%u",
                    instances[i]->name ? instances[i]->name : "?",
                    (unsigned)cpu, (unsigned)instances[i]->owner_cpu);
            return BM_ERR_INVALID;
        }
    }
    return BM_OK;
}

/**
 * @brief 批量初始化执行实例（校验、资源检查、HRT 与硬件绑定）
 *
 * @param instances 实例指针数组
 * @param count 实例数量
 * @return BM_OK 成功；负值为各阶段错误码
 */
int bm_exec_init_all(const bm_exec_t *const *instances, uint32_t count) {
    const bm_resource_claim_t *claim_ptrs[BM_CONFIG_MAX_EXEC_INSTANCES];
    uint32_t claim_counts[BM_CONFIG_MAX_EXEC_INSTANCES];
    uint32_t i;
    uint32_t s;
    int rc;

    if (!exec_cpu_valid() || !instances || count == 0u ||
        count > BM_CONFIG_MAX_EXEC_INSTANCES) {
        BM_LOGE("exec", "init_all invalid count=%u", (unsigned)count);
        return BM_ERR_INVALID;
    }

    exec_teardown_session();

    rc = validate_unique_ids(instances, count);
    if (rc != BM_OK) {
        BM_LOGE("exec", "init_all duplicate instance id");
        return rc;
    }
    rc = validate_instances_on_this_cpu(instances, count);
    if (rc != BM_OK) {
        return rc;
    }
    for (i = 0u; i < count; ++i) {
        if (!instances[i]) {
            return BM_ERR_INVALID;
        }
        rc = validate_instance(instances[i]);
        if (rc != BM_OK) {
            return rc;
        }
        claim_ptrs[i] = instances[i]->claims;
        claim_counts[i] = instances[i]->claim_count;
    }

    rc = bm_resource_check_conflicts(claim_ptrs, claim_counts, count);
    if (rc != BM_OK) {
        BM_LOGE("exec", "init_all resource conflict rc=%d", rc);
        return rc;
    }

    for (i = 0u; i < count; ++i) {
        g_instances[i] = instances[i];
    }
    g_instance_count = count;

    rc = assemble_tables(instances, count);
    if (rc != BM_OK) {
        exec_clear_runtime();
        return rc;
    }

    if (g_hrt_slot_count > 0u) {
        rc = bm_hrt_init(g_hrt_slots, g_hrt_slot_count);
        if (rc != BM_OK) {
            exec_clear_runtime();
            return rc;
        }
    }

    for (i = 0u; i < count; ++i) {
        rc = instances[i]->ops->init(instances[i]);
        if (rc != BM_OK) {
            BM_LOGE("exec", "init failed inst id=%u rc=%d",
                    (unsigned)instances[i]->id, rc);
            exec_abort_session();
            return rc;
        }
        g_init_done_count++;
    }

    rc = validate_stream_owner();
    if (rc != BM_OK) {
        exec_abort_session();
        return rc;
    }

    for (i = 0u; i < count; ++i) {
        const bm_exec_t *inst = instances[i];
        for (s = 0u; s < inst->slot_count; ++s) {
            const bm_exec_slot_t *slot = &inst->slots[s];
            bm_hal_hrt_binding_t hal_binding;
            bm_exec_binding_t *binding = NULL;
            uint32_t b;

            if (slot->kind != BM_EXEC_SLOT_HARDWARE) {
                continue;
            }

            hal_binding.callback = bm_exec_run_binding;
            for (b = 0u; b < g_binding_count; ++b) {
                if (g_bindings[b].instance == inst &&
                    g_bindings[b].slot == slot) {
                    binding = &g_bindings[b];
                    break;
                }
            }
            if (!binding) {
                rc = BM_ERR_INVALID;
            } else {
                hal_binding.context = binding;
                rc = slot->bind(inst, &hal_binding);
            }
            if (rc != BM_OK) {
                exec_abort_session();
                return rc;
            }
        }
    }

    exec_set_session(BM_EXEC_SESSION_INITED);
    BM_LOGI("exec", "init_all ok count=%u hrt_slots=%u",
            (unsigned)count, (unsigned)g_hrt_slot_count);
    return BM_OK;
}

/**
 * @brief 批量启动已初始化的执行实例
 *
 * @param instances 实例指针数组（须与 init_all 时一致）
 * @param count 实例数量
 * @return BM_OK 成功；BM_ERR_INVALID 参数不匹配；负值为 start 失败码
 */
static int exec_ensure_hrt_started(void) {
    int rc;

    if (g_hrt_slot_count == 0u) {
        return BM_OK;
    }
    rc = bm_hrt_start();
    if (rc == BM_ERR_ALREADY) {
        return BM_OK;
    }
    return rc;
}

int bm_exec_start_all(const bm_exec_t *const *instances, uint32_t count) {
    uint32_t i;
    int rc;

    if (!exec_cpu_valid() || !instances || count == 0u ||
        count != g_instance_count) {
        return BM_ERR_INVALID;
    }
    /* atomic acquire-load 保证状态可见性 */
    if (exec_get_session(bm_exec_this()) == BM_EXEC_SESSION_STARTED) {
        return BM_ERR_ALREADY;
    }
    if (exec_get_session(bm_exec_this()) != BM_EXEC_SESSION_INITED) {
        return BM_ERR_NOT_INIT;
    }

    for (i = 0u; i < count; ++i) {
        if (!instances[i] || instances[i] != g_instances[i]) {
            return BM_ERR_INVALID;
        }
        rc = instances[i]->ops->start(instances[i]);
        if (rc != BM_OK) {
            BM_LOGE("exec", "start failed inst id=%u rc=%d",
                    (unsigned)instances[i]->id, rc);
            exec_abort_session();
            return rc;
        }
    }

#if BM_CPU_LOCAL_ENABLE_ROUTE
    exec_set_session(BM_EXEC_SESSION_STARTED);
    BM_LOGI("exec", "prepare ok count=%u", (unsigned)count);
    return BM_OK;
#else
    rc = exec_ensure_hrt_started();
    if (rc != BM_OK) {
        BM_LOGE("exec", "hrt start failed rc=%d", rc);
        exec_abort_session();
        return rc;
    }
    exec_set_session(BM_EXEC_SESSION_STARTED);
    BM_LOGI("exec", "start_all ok count=%u", (unsigned)count);
    return BM_OK;
#endif
}

/**
 * @brief 释放所有 IRQ/定时器资源，启动 HRT
 *
 * @return BM_OK 成功；负值表示失败
 */
int bm_exec_irq_release_all(void) {
#if BM_CPU_LOCAL_ENABLE_ROUTE
    int rc;

    if (!exec_cpu_valid()) {
        return BM_ERR_INVALID;
    }
    if (exec_get_session(bm_exec_this()) != BM_EXEC_SESSION_STARTED) {
        return BM_ERR_NOT_INIT;
    }
    rc = exec_ensure_hrt_started();
    if (rc != BM_OK) {
        BM_LOGE("exec", "irq_release hrt start failed rc=%d", rc);
        exec_abort_session();
        return rc;
    }
    BM_LOGI("exec", "irq_release ok");
    return BM_OK;
#else
    return BM_OK;
#endif
}

/**
 * @brief 在当前 CPU 上过滤并初始化属于该核的实例
 *
 * @param instances 全局实例指针数组
 * @param count 实例数量
 * @return BM_OK 成功；负值表示失败
 */
int bm_exec_init_on_this_cpu(const bm_exec_t *const *instances, uint32_t count) {
    const bm_exec_t *filtered[BM_CONFIG_MAX_EXEC_INSTANCES];
    uint32_t cpu = bm_hal_cpu_id();
    uint32_t n = 0u;
    uint32_t i;

    if (!instances || count == 0u) {
        return BM_ERR_INVALID;
    }
    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return BM_ERR_INVALID;
    }
    for (i = 0u; i < count; ++i) {
        if (instances[i] && instances[i]->owner_cpu == (uint8_t)cpu) {
            filtered[n++] = instances[i];
        }
    }
    if (n == 0u) {
        return BM_OK;
    }
    return bm_exec_init_all(filtered, n);
}

/**
 * @brief 在当前 CPU 上准备（启动）属于该核的实例
 *
 * @param instances 全局实例指针数组
 * @param count 实例数量
 * @return BM_OK 成功；负值表示失败
 */
int bm_exec_prepare_on_this_cpu(const bm_exec_t *const *instances,
                                uint32_t count) {
    const bm_exec_t *filtered[BM_CONFIG_MAX_EXEC_INSTANCES];
    uint32_t cpu = bm_hal_cpu_id();
    uint32_t n = 0u;
    uint32_t i;

    if (!instances || count == 0u) {
        return BM_ERR_INVALID;
    }
    for (i = 0u; i < count; ++i) {
        if (instances[i] && instances[i]->owner_cpu == (uint8_t)cpu) {
            filtered[n++] = instances[i];
        }
    }
    if (n == 0u) {
        return BM_OK;
    }
    return bm_exec_start_all(filtered, n);
}

/**
 * @brief 在当前 CPU 上调用 IRQ 释放门控并释放 IRQ 资源
 *
 * @return BM_OK 成功；负值表示失败
 */
int bm_exec_irq_release_on_this_cpu(void) {
    if (!exec_cpu_valid()) {
        return BM_ERR_INVALID;
    }
    if (s_irq_release_gate && s_irq_release_gate() != BM_OK) {
        return BM_ERR_NOT_INIT;
    }
    return bm_exec_irq_release_all();
}

/**
 * @brief 安全停止所有实例并释放 HRT/硬件绑定
 *
 * @param instances 实例指针数组（可为 NULL，则使用内部记录）
 * @param count 实例数量
 */
void bm_exec_safe_stop_all(const bm_exec_t *const *instances,
                           uint32_t count) {
    if (!exec_cpu_valid()) {
        return;
    }
    if ((instances == NULL && count > 0u) ||
        (instances != NULL && count != g_instance_count)) {
        BM_LOGW("exec", "safe_stop_all ignored external instance table");
    }

    exec_teardown_session();
    BM_LOGI("exec", "safe_stop_all done");
}

/**
 * @brief 查询当前执行批次会话状态
 *
 * @return 临界区内读取的会话状态快照
 */
bm_exec_session_t bm_exec_get_session(void) {
    bm_exec_cpu_state_t *state = bm_exec_this();
    bm_irq_state_t irq_state = BM_CRITICAL_ENTER();
    bm_exec_session_t session = state
                                    ? exec_get_session(state)
                                    : BM_EXEC_SESSION_NONE;

    BM_CRITICAL_EXIT(irq_state);
    return session;
}

/**
 * @brief 按 ID 在实例数组中查找执行实例
 *
 * @param instances 实例指针数组
 * @param count 实例数量
 * @param id 目标实例 ID
 * @return 匹配的实例指针；未找到返回 NULL
 */
const bm_exec_t *bm_exec_find(const bm_exec_t *const *instances,
                                   uint32_t count,
                                   uint32_t id) {
    uint32_t i;

    if (!instances) {
        return NULL;
    }

    for (i = 0u; i < count; ++i) {
        if (instances[i] && instances[i]->id == id) {
            return instances[i];
        }
    }

    return NULL;
}
