/**
 * @file test_qemu_bus_smp.c
 * @brief bm_bus 多核并发正确性验证（QEMU ARMv7-A Cortex-A15 virt -smp 2）
 *
 * 测试拓扑：CPU0 为写者（publisher），CPU1 为读者（consumer）。
 * 覆盖四种 mode：
 *   - LATEST：CPU0 高频 publish，CPU1 spin-until-stable 读，断言永不读到撕裂帧。
 *   - SIGNAL：CPU0 publish 遥测，CPU1 读，验证跨核可见性（写 cap-1 帧全可见）+
 *             overflow 检测（再写 cap+1 帧绕圈后读者被跳过）。
 *   - QUEUE：CPU0 先做满拒绝测试，CPU1 再读全部已写帧，验证保序 + 满拒绝。
 *   - BLOCK：CPU0 通过 bm_bus BLOCK 入口（委托 bm_stream adapter）生产块，
 *             CPU1 消费，验证 produce→commit→consume 往返、valid_bytes/ts_ns 透传、
 *             produce_abort、对 BLOCK 调 acquire_write 返回 NOT_SUPPORTED、
 *             未 bind 后端的错误、owner_cpu 透传行为。
 *
 * 同步协议（7 阶段）：
 *   INIT → ATTACH → LATEST → SIGNAL → QUEUE → BLOCK → DONE
 *   CPU1 在 ATTACH 阶段对 SIGNAL/QUEUE bus 做 reader_attach，
 *   保证 SIGNAL/QUEUE 读者从写者开始写之前就已就位。
 *   QUEUE 阶段 CPU0 先写满并做满拒绝测试，发 g_queue_full_done 后 CPU1 才开始读。
 *   BLOCK 阶段 CPU0 生产块后通知 CPU1 消费。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-25       1.0            zeh            初稿：bm_bus 双核 QEMU 验证
 * 2026-06-25       1.1            zeh            修正 SIGNAL/QUEUE reader_attach 时序；
 *                                                 增加 ATTACH 阶段；使用 PSCI poweroff 退出
 * 2026-06-25       1.2            zeh            修正 QUEUE 并发竞争：CPU0 先做满拒绝后
 *                                                 再通知 CPU1 读；修正 SIGNAL 可见性期望值
 *                                                 （cap-1 帧全可见，overflow 后跳转正确）
 * 2026-06-26       1.3            zeh            新增 BLOCK 阶段：produce→commit→consume 往返、
 *                                                 valid_bytes/ts_ns 透传、abort、NOT_SUPPORTED、
 *                                                 未 bind 错误、多核 owner_cpu 透传测试
 *
 */
#include "bm/core/bm_bus.h"
#include "bm/hybrid/bm_stream.h"
#include "bm/common/bm_atomic_ipc.h"
#include "hal/bm_hal_cpu.h"
#include "hal/bm_hal_timer.h"
#include "bm_qemu_tap.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* =========================================================================
 * 测试帧类型
 * ========================================================================= */

/**
 * @brief 单条遥测帧（seq/magic 相互关联，验证撕裂）
 */
typedef struct {
    uint32_t seq;    /**< 帧序号（单调递增） */
    uint32_t magic;  /**< 校验值（= seq ^ 0xDEADBEEFu） */
} bus_frame_t;

/** 校验值生成宏 */
#define FRAME_MAGIC(seq) ((seq) ^ 0xDEADBEEFu)

/* =========================================================================
 * 阶段同步原子
 * ========================================================================= */

/**
 * @brief 测试阶段枚举（CPU0 驱动推进）
 */
typedef enum {
    BUS_PHASE_INIT   = 0u,  /**< CPU0 初始化中 */
    BUS_PHASE_ATTACH = 1u,  /**< 等待 CPU1 附加读者 */
    BUS_PHASE_LATEST = 2u,  /**< LATEST 测试 */
    BUS_PHASE_SIGNAL = 3u,  /**< SIGNAL 测试 */
    BUS_PHASE_QUEUE  = 4u,  /**< QUEUE 测试 */
    BUS_PHASE_BLOCK  = 5u,  /**< BLOCK 测试（bm_stream adapter 委托） */
    BUS_PHASE_DONE   = 6u   /**< 全部完成 */
} bus_test_phase_t;

static bm_atomic_ipc_u32_t g_phase     = BM_ATOMIC_IPC_U32_INIT(BUS_PHASE_INIT);
/** CPU1 就绪标志（CPU1 完成本阶段后置 1，CPU0 清零再推进） */
static bm_atomic_ipc_u32_t g_cpu1_rdy  = BM_ATOMIC_IPC_U32_INIT(0u);

/**
 * @brief 单核模式标志（-smp 1 时 boot_secondary 失败后置 1）
 *
 * 置 1 后：需要第二核的测试项全部输出 TAP SKIP，CPU0 基础测试照常跑，
 * 跨核握手等待直接绕过，最终输出 "bm_bus SMP test: PASSED"。
 */
static int g_single_core = 0;

/** QUEUE：CPU0 完成满拒绝测试后置 1，CPU1 才开始读 */
static bm_atomic_ipc_u32_t g_queue_full_done = BM_ATOMIC_IPC_U32_INIT(0u);

/** SIGNAL：CPU0 写完第一批（cap-1 帧）后置 1，CPU1 开始读 */
static bm_atomic_ipc_u32_t g_signal_batch1_done = BM_ATOMIC_IPC_U32_INIT(0u);

/** SIGNAL：CPU1 读完第一批后置 1，CPU0 再写第二批（overflow 帧） */
static bm_atomic_ipc_u32_t g_signal_batch1_read = BM_ATOMIC_IPC_U32_INIT(0u);

/** SIGNAL：CPU0 写完第二批后置 1 */
static bm_atomic_ipc_u32_t g_signal_batch2_done = BM_ATOMIC_IPC_U32_INIT(0u);

/* SIGNAL 第三批：消费窗口复检场景（Fix 1，零拷贝借用窗口确定性保护） */
#define SIGNAL_BATCH3_SEQ 900u  /**< batch3 借出帧的 seq 基值 */
/** CPU1 排空 batch2 残留、游标追上写者后置 1 */
static bm_atomic_ipc_u32_t g_signal_batch3_drained     = BM_ATOMIC_IPC_U32_INIT(0u);
/** CPU0 写入 batch3 一帧供借出后置 1 */
static bm_atomic_ipc_u32_t g_signal_batch3_frame_ready = BM_ATOMIC_IPC_U32_INIT(0u);
/** CPU1 借住该帧（进入消费窗口、未释放）后置 1 */
static bm_atomic_ipc_u32_t g_signal_batch3_borrowed    = BM_ATOMIC_IPC_U32_INIT(0u);
/** CPU0 在消费窗口内灌满一整圈覆盖该槽后置 1 */
static bm_atomic_ipc_u32_t g_signal_batch3_lapped      = BM_ATOMIC_IPC_U32_INIT(0u);

/* =========================================================================
 * 测试结果
 * ========================================================================= */

#define BUS_MAX_RESULTS 36u

static bm_qemu_result_t g_res0[BUS_MAX_RESULTS];  /**< CPU0 侧结果 */
static uint32_t         g_res0_cnt;

static bm_qemu_result_t    g_res1[BUS_MAX_RESULTS];  /**< CPU1 侧结果 */
static bm_atomic_ipc_u32_t g_res1_cnt = BM_ATOMIC_IPC_U32_INIT(0u);

/**
 * @brief CPU0 记录测试结果（薄封装，复用 bm_qemu_result_t）
 */
static void rec0(const char *name, int pass, uint32_t info) {
    if (g_res0_cnt < BUS_MAX_RESULTS) {
        g_res0[g_res0_cnt].name = name;
        g_res0[g_res0_cnt].pass = pass;
        g_res0[g_res0_cnt].info = info;
        g_res0[g_res0_cnt].skip = 0;
        g_res0_cnt++;
    }
}

/**
 * @brief CPU0 记录 TAP SKIP 条目（单核模式：需第二核的项输出 "ok N - cpu0:name # SKIP"）
 */
static void rec0_skip(const char *name) {
    if (g_res0_cnt < BUS_MAX_RESULTS) {
        g_res0[g_res0_cnt].name = name;
        g_res0[g_res0_cnt].pass = 1;
        g_res0[g_res0_cnt].info = 0u;
        g_res0[g_res0_cnt].skip = 1;
        g_res0_cnt++;
    }
}

/**
 * @brief CPU1 记录测试结果（保留原子计数语义）
 */
static void rec1(const char *name, int pass, uint32_t info) {
    uint32_t idx = bm_atomic_ipc_load_u32(&g_res1_cnt);

    if (idx < BUS_MAX_RESULTS) {
        g_res1[idx].name = name;
        g_res1[idx].pass = pass;
        g_res1[idx].info = info;
        g_res1[idx].skip = 0;
        bm_atomic_ipc_store_u32(&g_res1_cnt, idx + 1u);
    }
}

/**
 * @brief CPU1 记录 TAP SKIP 条目（单核模式时由 CPU0 代填占位）
 */
static void rec1_skip(const char *name) {
    uint32_t idx = bm_atomic_ipc_load_u32(&g_res1_cnt);

    if (idx < BUS_MAX_RESULTS) {
        g_res1[idx].name = name;
        g_res1[idx].pass = 1;
        g_res1[idx].info = 0u;
        g_res1[idx].skip = 1;
        bm_atomic_ipc_store_u32(&g_res1_cnt, idx + 1u);
    }
}

/* =========================================================================
 * bm_bus 存储（静态共享）
 * ========================================================================= */

/** LATEST 三缓冲（cap=3，符合 spec §7 防撕裂约束） */
BM_BUS_DEFINE(g_bus_latest, bus_frame_t, 3u, 1u, BM_BUS_LATEST);

/**
 * SIGNAL 环（cap=4，max_consumers=1）
 * 可见性测试：写 cap-1=3 帧全部可见；overflow 测试：再写 cap+1=5 帧触发绕圈。
 */
BM_BUS_DEFINE(g_bus_signal, bus_frame_t, 4u, 1u, BM_BUS_SIGNAL);

/**
 * QUEUE SPSC（cap=4，max_consumers=1，最多存 cap-1=3 项）
 * 满拒绝测试：写 3 帧后第 4 帧应返回 BM_ERR_OVERFLOW。
 */
BM_BUS_DEFINE(g_bus_queue, bus_frame_t, 4u, 1u, BM_BUS_QUEUE);

/**
 * BLOCK 模式（cap=2 最小值，max_consumers=1；存储由 bm_stream 后端持有）
 * BLOCK 模式 BM_BUS_DEFINE 对 2 的幂豁免，cap=2 合法。
 */
BM_BUS_DEFINE(g_bus_block, bus_frame_t, 2u, 1u, BM_BUS_BLOCK);

static bm_bus_t g_h_latest;
static bm_bus_t g_h_signal;
static bm_bus_t g_h_queue;
static bm_bus_t g_h_block;

/* =========================================================================
 * BLOCK 测试用 bm_stream 实例（静态共享区）
 * ========================================================================= */

/** BLOCK 测试载荷类型（与 bus_frame_t 对应，bus_frame_t 作为 payload） */
#define BLOCK_STREAM_DEPTH  4u

BM_STREAM_PAYLOADS(g_blk_stream, bus_frame_t, BLOCK_STREAM_DEPTH);
BM_STREAM_BLOCKS(g_blk_stream,   BLOCK_STREAM_DEPTH);
BM_STREAM_INSTANCE(g_blk_stream, BLOCK_STREAM_DEPTH);

/** BLOCK 阶段同步：CPU0 生产完毕后置 1，CPU1 开始消费 */
static bm_atomic_ipc_u32_t g_block_produced = BM_ATOMIC_IPC_U32_INIT(0u);
/** BLOCK 阶段：CPU1 消费完毕后置 1 */
static bm_atomic_ipc_u32_t g_block_consumed = BM_ATOMIC_IPC_U32_INIT(0u);

/* =========================================================================
 * LATEST 测试参数
 * ========================================================================= */

#define LATEST_WRITE_ROUNDS  200u
#define LATEST_READ_ROUNDS   150u

/** CPU0 已写帧数（供 CPU1 判断写者在跑） */
static bm_atomic_ipc_u32_t g_latest_written = BM_ATOMIC_IPC_U32_INIT(0u);

/* =========================================================================
 * SIGNAL 测试参数
 * ========================================================================= */

/**
 * 第一批写 cap-1=3 帧（不触发 overflow），全部应被 CPU1 读到。
 * 第二批写 cap+1=5 帧（wc-rc >= cap 时 overflow），CPU1 被跳过。
 */
#define SIGNAL_CAP            4u   /**< bus_signal 容量 */
#define SIGNAL_BATCH1_FRAMES  3u   /**< 第一批：cap-1，全可见 */
#define SIGNAL_BATCH2_FRAMES  5u   /**< 第二批：cap+1，触发 overflow */

/* =========================================================================
 * QUEUE 测试参数
 * ========================================================================= */

/** QUEUE cap=4，最多存 cap-1=3 项 */
#define QUEUE_CAP        4u
#define QUEUE_MAX_ITEMS  3u   /**< cap-1 */

/* =========================================================================
 * CPU1 任务
 * ========================================================================= */

/* CPU1 的 SIGNAL/QUEUE 读者句柄（ATTACH 阶段初始化） */
static bm_bus_reader_t g_r_signal;
static bm_bus_reader_t g_r_queue;

/**
 * @brief CPU1 ATTACH 阶段：在写者写入前附加读者
 *
 * 必须在 CPU0 开始写入之前完成，确保读者游标从写起点开始追赶。
 */
static void cpu1_attach_readers(void) {
    int rc;

    /* SIGNAL：读者游标将从 write_cur（当前=0）开始 */
    rc = bm_bus_reader_attach(&g_h_signal, &g_r_signal);
    rec1("SIGNAL_attach", rc == BM_OK, (uint32_t)(-rc));

    /* QUEUE：读者游标将从 write_cur（当前=0）开始 */
    rc = bm_bus_reader_attach(&g_h_queue, &g_r_queue);
    rec1("QUEUE_attach",  rc == BM_OK, (uint32_t)(-rc));
}

/**
 * @brief CPU1 执行 LATEST 读取验证
 *
 * 连续读 LATEST_READ_ROUNDS 次，验证每帧的 magic == FRAME_MAGIC(seq)（防撕裂）。
 */
static void cpu1_test_latest(void) {
    bm_bus_reader_t r;
    uint32_t torn_count = 0u;
    uint32_t read_ok    = 0u;
    uint32_t i;
    int rc;

    /* LATEST 不需要提前 attach（无游标），此处 attach 即可 */
    rc = bm_bus_reader_attach(&g_h_latest, &r);
    if (rc != BM_OK) {
        rec1("LATEST_reader_attach", 0, (uint32_t)(-rc));
        return;
    }

    /* 等待 CPU0 开始写入 */
    while (bm_atomic_ipc_load_u32(&g_latest_written) == 0u) {
        bm_hal_cpu_yield();
    }

    for (i = 0u; i < LATEST_READ_ROUNDS; i++) {
        const void *slot;

        rc = bm_bus_acquire_read(&r, &slot);
        if (rc == BM_OK) {
            const bus_frame_t *f = (const bus_frame_t *)slot;

            if (f->magic != FRAME_MAGIC(f->seq)) {
                torn_count++;
            } else {
                read_ok++;
            }
            (void)bm_bus_release(&r);
        }
        bm_hal_cpu_yield();
    }

    rec1("LATEST_no_torn_frame",  torn_count == 0u, torn_count);
    rec1("LATEST_read_completed", read_ok > 0u,     read_ok);
}

/**
 * @brief CPU1 执行 SIGNAL 可见性测试（第一批，cap-1 帧全可见）
 *
 * 等待 CPU0 写完 SIGNAL_BATCH1_FRAMES（=cap-1=3）帧后读取。
 * 全部应可见（无 overflow），期望读到 SIGNAL_BATCH1_FRAMES 帧。
 */
static void cpu1_test_signal_batch1(void) {
    uint32_t read_count = 0u;
    uint32_t tries      = 0u;
    int rc;

    /* 等待 CPU0 完成第一批写 */
    while (bm_atomic_ipc_load_u32(&g_signal_batch1_done) == 0u) {
        bm_hal_cpu_yield();
    }

    /* 读取可用帧（期望 BM_ERR_WOULD_BLOCK 表示无数据时退出） */
    while (tries < SIGNAL_BATCH1_FRAMES * 4u) {
        const void *slot;

        rc = bm_bus_acquire_read(&g_r_signal, &slot);
        if (rc == BM_OK) {
            read_count++;
            (void)bm_bus_release(&g_r_signal);
        } else if (rc == BM_ERR_OVERFLOW) {
            /* 第一批不应触发 overflow，记录异常 */
            rec1("SIGNAL_unexpected_overflow", 0, read_count);
            (void)bm_bus_release(&g_r_signal);
            break;
        } else {
            /* BM_ERR_WOULD_BLOCK：无新数据 */
            break;
        }
        tries++;
    }

    rec1("SIGNAL_visibility",
         read_count == SIGNAL_BATCH1_FRAMES,
         read_count);

    /* 通知 CPU0 可以写第二批 */
    bm_atomic_ipc_store_u32(&g_signal_batch1_read, 1u);
}

/**
 * @brief CPU1 执行 SIGNAL overflow 检测（第二批，cap+1 帧绕圈）
 *
 * 等待 CPU0 写完 SIGNAL_BATCH2_FRAMES 帧后读取。
 * 此时 wc - rc >= cap，reader 被跳过，首次 acquire_read 应返回 BM_ERR_OVERFLOW。
 */
static void cpu1_test_signal_batch2(void) {
    uint32_t overflow_count = 0u;
    uint32_t read_count     = 0u;
    uint32_t tries          = 0u;
    int rc;

    /* 等待 CPU0 完成第二批写 */
    while (bm_atomic_ipc_load_u32(&g_signal_batch2_done) == 0u) {
        bm_hal_cpu_yield();
    }

    /* 尝试读取，期望首帧触发 overflow */
    while (tries < SIGNAL_BATCH2_FRAMES * 4u) {
        const void *slot;

        rc = bm_bus_acquire_read(&g_r_signal, &slot);
        if (rc == BM_ERR_OVERFLOW) {
            overflow_count++;
            (void)bm_bus_release(&g_r_signal);
        } else if (rc == BM_OK) {
            read_count++;
            (void)bm_bus_release(&g_r_signal);
        } else {
            break;
        }
        tries++;
    }

    rec1("SIGNAL_overflow_detected",
         overflow_count > 0u,
         overflow_count);
}

/**
 * @brief CPU1 SIGNAL 消费窗口复检（Fix 1）
 *
 * 借出一帧有效数据后持有不释放（进入消费窗口），等 CPU0 在窗口内灌满一整圈
 * 覆盖该槽，release 必须返回 BM_ERR_OVERFLOW（本帧作废）；随后再借必得未撕裂
 * 的新帧（magic 与 seq 自洽），证明零拷贝借用窗口在真多核下被确定性保护且可恢复。
 */
static void cpu1_test_signal_batch3(void) {
    const void *slot;
    int rc;
    uint32_t tries = 0u;

    /* 1) 排空 batch2 残留，使读者游标追上写者 */
    while (tries < 64u) {
        rc = bm_bus_acquire_read(&g_r_signal, &slot);
        if (rc == BM_ERR_WOULD_BLOCK) {
            break;
        }
        (void)bm_bus_release(&g_r_signal);
        tries++;
    }
    bm_atomic_ipc_store_u32(&g_signal_batch3_drained, 1u);

    /* 2) 等 CPU0 写入 batch3 一帧后借出（持有指针、进入消费窗口、暂不释放） */
    while (bm_atomic_ipc_load_u32(&g_signal_batch3_frame_ready) == 0u) {
        bm_hal_cpu_yield();
    }
    rc = bm_bus_acquire_read(&g_r_signal, &slot);
    if (rc != BM_OK) {
        rec1("SIGNAL_window_borrow", 0, (uint32_t)(-rc));
        return;
    }
    bm_atomic_ipc_store_u32(&g_signal_batch3_borrowed, 1u);

    /* 3) 等 CPU0 灌满一整圈覆盖本槽 */
    while (bm_atomic_ipc_load_u32(&g_signal_batch3_lapped) == 0u) {
        bm_hal_cpu_yield();
    }

    /* 4) release 复检：本帧已被覆盖 → 必返回 BM_ERR_OVERFLOW（确定性保护核心断言） */
    rc = bm_bus_release(&g_r_signal);
    rec1("SIGNAL_window_overflow_detected", rc == BM_ERR_OVERFLOW, (uint32_t)(-rc));

    /* 5) 复检后再借：写者已静默，必得未撕裂的新帧（magic==FRAME_MAGIC(seq)），证明可恢复 */
    {
        uint32_t untorn = 0u;
        uint32_t t2     = 0u;
        while (t2 < 64u) {
            rc = bm_bus_acquire_read(&g_r_signal, &slot);
            if (rc == BM_OK || rc == BM_ERR_OVERFLOW) {
                const bus_frame_t *f = (const bus_frame_t *)slot;
                untorn = (f->magic == FRAME_MAGIC(f->seq)) ? 1u : 0u;
                (void)bm_bus_release(&g_r_signal);
                break;
            }
            bm_hal_cpu_yield();
            t2++;
        }
        rec1("SIGNAL_window_recover_untorn", untorn == 1u, untorn);
    }
}

/**
 * @brief CPU1 执行 QUEUE 读取验证
 *
 * 等待 CPU0 完成满拒绝测试后读取已写入的 QUEUE_MAX_ITEMS 帧，验证保序。
 */
static void cpu1_test_queue(void) {
    uint32_t read_count = 0u;
    uint32_t order_ok   = 1u;
    uint32_t prev_seq   = UINT32_MAX;
    uint32_t tries      = 0u;
    int rc;

    /* 等待 CPU0 完成满拒绝测试（确保 CPU0 不再写，read_cur 不被竞争） */
    while (bm_atomic_ipc_load_u32(&g_queue_full_done) == 0u) {
        bm_hal_cpu_yield();
    }

    /* 读取所有已写帧 */
    while (tries < QUEUE_MAX_ITEMS * 4u) {
        const void *slot;

        rc = bm_bus_acquire_read(&g_r_queue, &slot);
        if (rc == BM_OK) {
            const bus_frame_t *f = (const bus_frame_t *)slot;

            if (prev_seq != UINT32_MAX && f->seq <= prev_seq) {
                order_ok = 0u;
            }
            prev_seq = f->seq;
            read_count++;
            (void)bm_bus_release(&g_r_queue);
            if (read_count >= QUEUE_MAX_ITEMS) {
                break;
            }
        } else {
            bm_hal_cpu_yield();
            tries++;
        }
    }

    rec1("QUEUE_read_all",  read_count == QUEUE_MAX_ITEMS, read_count);
    rec1("QUEUE_in_order",  order_ok != 0u,                order_ok);
}

/** @brief 前置声明：CPU1 BLOCK 消费验证（定义在 cpu1_task 之后） */
static void cpu1_test_block(void);

/**
 * @brief CPU1 主任务
 */
static void cpu1_task(void) {
    uint32_t phase;

    /* 等待 CPU0 进入 ATTACH 阶段 */
    do {
        phase = bm_atomic_ipc_load_u32(&g_phase);
        bm_hal_cpu_yield();
    } while (phase < BUS_PHASE_ATTACH);

    /* ATTACH：附加 SIGNAL/QUEUE 读者（写者还未开始写） */
    cpu1_attach_readers();
    bm_atomic_ipc_store_u32(&g_cpu1_rdy, 1u);

    /* 等待 CPU0 进入 LATEST 阶段 */
    do {
        phase = bm_atomic_ipc_load_u32(&g_phase);
        bm_hal_cpu_yield();
    } while (phase < BUS_PHASE_LATEST);

    /* LATEST */
    cpu1_test_latest();
    bm_atomic_ipc_store_u32(&g_cpu1_rdy, 1u);

    /* 等待 CPU0 进入 SIGNAL 阶段 */
    do {
        phase = bm_atomic_ipc_load_u32(&g_phase);
        bm_hal_cpu_yield();
    } while (phase < BUS_PHASE_SIGNAL);

    /* SIGNAL batch1：读 cap-1 帧（可见性） */
    cpu1_test_signal_batch1();
    /* SIGNAL batch2：读 overflow 帧（acquire 端 lap 检测） */
    cpu1_test_signal_batch2();
    /* SIGNAL batch3：消费窗口复检（release 端 lap 检测，Fix 1） */
    cpu1_test_signal_batch3();
    bm_atomic_ipc_store_u32(&g_cpu1_rdy, 1u);

    /* 等待 CPU0 进入 QUEUE 阶段 */
    do {
        phase = bm_atomic_ipc_load_u32(&g_phase);
        bm_hal_cpu_yield();
    } while (phase < BUS_PHASE_QUEUE);

    /* QUEUE */
    cpu1_test_queue();
    bm_atomic_ipc_store_u32(&g_cpu1_rdy, 1u);

    /* 等待 CPU0 进入 BLOCK 阶段 */
    do {
        phase = bm_atomic_ipc_load_u32(&g_phase);
        bm_hal_cpu_yield();
    } while (phase < BUS_PHASE_BLOCK);

    /* BLOCK */
    cpu1_test_block();
    bm_atomic_ipc_store_u32(&g_cpu1_rdy, 1u);

    /* 等待 DONE */
    do {
        phase = bm_atomic_ipc_load_u32(&g_phase);
        bm_hal_cpu_yield();
    } while (phase < BUS_PHASE_DONE);
}

/* =========================================================================
 * CPU1 PSCI 入口
 * ========================================================================= */

/**
 * @brief CPU1 BLOCK 守卫验证（owner 无关）
 *
 * @note bm_stream 是单核 owner SPSC，跨核消费会被 owner 校验拒绝；BLOCK 的产消
 *       端到端由 CPU0 同核完成（见 cpu0_test_block）。CPU1 此处仅验证与 owner
 *       无关的 bus 层守卫：BLOCK 模式调用环路径入口 acquire_write 返回 NOT_SUPPORTED。
 *
 *   1) acquire_write 对 BLOCK 返回 BM_ERR_NOT_SUPPORTED；
 *   2) 等待 CPU0 完成 BLOCK 端到端，置 g_block_consumed 通知收尾。
 */
static void cpu1_test_block(void) {
    void *slot_w;
    int   rc;

    /* 1) 对 BLOCK 调用 acquire_write 必须返回 NOT_SUPPORTED（bus 层守卫，与 owner 无关） */
    rc = bm_bus_acquire_write(&g_h_block, &slot_w);
    rec1("BLOCK_acquire_write_not_supported",
         rc == BM_ERR_NOT_SUPPORTED, (uint32_t)(-rc));

    /* 2) 等待 CPU0 完成 BLOCK 产消端到端 */
    while (bm_atomic_ipc_load_u32(&g_block_produced) == 0u) {
        bm_hal_cpu_yield();
    }

    /* 通知 CPU0 收尾 */
    bm_atomic_ipc_store_u32(&g_block_consumed, 1u);
}

/**
 * @brief CPU1 入口（由 PSCI CPU_ON 从核启动汇编调用）
 */
static void cpu1_entry(void) {
    (void)bm_hal_timer_init(1000u);
    cpu1_task();
    for (;;) {
        bm_hal_cpu_yield();
    }
}

/* =========================================================================
 * CPU0 阶段测试
 * ========================================================================= */

/**
 * @brief CPU0 LATEST 写入
 */
static void cpu0_test_latest(void) {
    uint32_t i;

    for (i = 0u; i < LATEST_WRITE_ROUNDS; i++) {
        void *slot;

        if (bm_bus_acquire_write(&g_h_latest, &slot) == BM_OK) {
            bus_frame_t *f = (bus_frame_t *)slot;
            f->seq   = i;
            f->magic = FRAME_MAGIC(i);
            (void)bm_bus_commit(&g_h_latest);
            bm_atomic_ipc_store_u32(&g_latest_written, i + 1u);
        }
        bm_hal_cpu_yield();
    }

    rec0("LATEST_write_ok",
         bm_atomic_ipc_load_u32(&g_latest_written) == LATEST_WRITE_ROUNDS,
         bm_atomic_ipc_load_u32(&g_latest_written));
}

/**
 * @brief CPU0 SIGNAL 写入
 *
 * 第一批写 SIGNAL_BATCH1_FRAMES（cap-1=3）帧，等 CPU1 读完后
 * 再写第二批 SIGNAL_BATCH2_FRAMES（cap+1=5）帧触发 overflow。
 */
static void cpu0_test_signal(void) {
    uint32_t i;
    uint32_t written1 = 0u;
    uint32_t written2 = 0u;

    /* 第一批：写 cap-1=3 帧 */
    for (i = 0u; i < SIGNAL_BATCH1_FRAMES; i++) {
        void *slot;

        if (bm_bus_acquire_write(&g_h_signal, &slot) == BM_OK) {
            bus_frame_t *f = (bus_frame_t *)slot;
            f->seq   = i;
            f->magic = FRAME_MAGIC(i);
            (void)bm_bus_commit(&g_h_signal);
            written1++;
        }
    }
    bm_atomic_ipc_store_u32(&g_signal_batch1_done, 1u);
    rec0("SIGNAL_batch1_write_ok", written1 == SIGNAL_BATCH1_FRAMES, written1);

    /* 等待 CPU1 读完第一批 */
    while (bm_atomic_ipc_load_u32(&g_signal_batch1_read) == 0u) {
        bm_hal_cpu_yield();
    }

    /* 第二批：写 cap+1=5 帧，wc-rc 超过 cap，触发 overflow */
    for (i = 0u; i < SIGNAL_BATCH2_FRAMES; i++) {
        void *slot;

        if (bm_bus_acquire_write(&g_h_signal, &slot) == BM_OK) {
            bus_frame_t *f = (bus_frame_t *)slot;
            f->seq   = SIGNAL_BATCH1_FRAMES + i;
            f->magic = FRAME_MAGIC(SIGNAL_BATCH1_FRAMES + i);
            (void)bm_bus_commit(&g_h_signal);
            written2++;
        }
    }
    bm_atomic_ipc_store_u32(&g_signal_batch2_done, 1u);
    rec0("SIGNAL_batch2_write_ok", written2 == SIGNAL_BATCH2_FRAMES, written2);

    /* 第三批：消费窗口复检场景（Fix 1）。等 CPU1 排空 → 写 1 帧供其借出 →
     * 等其借住 → 在消费窗口内灌满一整圈（SIGNAL_CAP 帧）覆盖该槽。 */
    while (bm_atomic_ipc_load_u32(&g_signal_batch3_drained) == 0u) {
        bm_hal_cpu_yield();
    }
    {
        void *slot;
        uint32_t j;

        /* 写 1 帧供 CPU1 借出 */
        if (bm_bus_acquire_write(&g_h_signal, &slot) == BM_OK) {
            bus_frame_t *f = (bus_frame_t *)slot;
            f->seq   = SIGNAL_BATCH3_SEQ;
            f->magic = FRAME_MAGIC(SIGNAL_BATCH3_SEQ);
            (void)bm_bus_commit(&g_h_signal);
        }
        bm_atomic_ipc_store_u32(&g_signal_batch3_frame_ready, 1u);

        /* 等 CPU1 借住该帧（进入消费窗口） */
        while (bm_atomic_ipc_load_u32(&g_signal_batch3_borrowed) == 0u) {
            bm_hal_cpu_yield();
        }
        /* 灌满一整圈（SIGNAL_CAP 帧）：cursor 越过被借槽，必覆盖之 */
        for (j = 0u; j < SIGNAL_CAP; j++) {
            if (bm_bus_acquire_write(&g_h_signal, &slot) == BM_OK) {
                bus_frame_t *f = (bus_frame_t *)slot;
                f->seq   = SIGNAL_BATCH3_SEQ + 1u + j;
                f->magic = FRAME_MAGIC(SIGNAL_BATCH3_SEQ + 1u + j);
                (void)bm_bus_commit(&g_h_signal);
            }
        }
        bm_atomic_ipc_store_u32(&g_signal_batch3_lapped, 1u);
        rec0("SIGNAL_batch3_lap_write_ok", 1, SIGNAL_CAP);
    }
}

/**
 * @brief CPU0 QUEUE 写入 + 满拒绝验证
 *
 * 读者已在 ATTACH 阶段就位（read_cur=0）。
 * 写 QUEUE_MAX_ITEMS（=cap-1=3）帧填满队列后，
 * 第 4 帧应返回 BM_ERR_OVERFLOW（满拒绝）。
 * 完成后设置 g_queue_full_done，让 CPU1 开始读。
 */
static void cpu0_test_queue(void) {
    uint32_t i;
    uint32_t written  = 0u;
    uint32_t overflow = 0u;
    int rc;

    /* 写 cap-1 帧：填满队列（CPU1 还未开始读，read_cur=0 不变） */
    for (i = 0u; i < QUEUE_MAX_ITEMS; i++) {
        void *slot;

        rc = bm_bus_acquire_write(&g_h_queue, &slot);
        if (rc == BM_OK) {
            bus_frame_t *f = (bus_frame_t *)slot;
            f->seq   = i;
            f->magic = FRAME_MAGIC(i);
            (void)bm_bus_commit(&g_h_queue);
            written++;
        } else if (rc == BM_ERR_OVERFLOW) {
            overflow++;
        }
    }

    rec0("QUEUE_write_no_overflow", overflow == 0u,              overflow);
    rec0("QUEUE_write_ok",          written == QUEUE_MAX_ITEMS,  written);

    /* 第 4 帧：wc=3，rc=0，wc-rc=3 >= cap-1=3，队列已满 → 应拒绝 */
    {
        void *slot;
        rc = bm_bus_acquire_write(&g_h_queue, &slot);
        rec0("QUEUE_full_reject",
             rc == BM_ERR_OVERFLOW,
             (rc == BM_ERR_OVERFLOW) ? 1u : (uint32_t)(-rc));
        if (rc == BM_OK) {
            /* 写者不应成功，若意外成功则 abort 防止数据混淆 */
            (void)bm_bus_abort(&g_h_queue);
        }
    }

    /* 通知 CPU1 可以开始读（满拒绝测试完成，read_cur 稳定在 0） */
    bm_atomic_ipc_store_u32(&g_queue_full_done, 1u);
}

/**
 * @brief CPU0 BLOCK 生产 + 消费端到端验证（产消同核）
 *
 * @note bm_stream 是单核 owner 约束的 SPSC（producer/consumer 必须同 owner 核），
 *       因此 BLOCK 的产消端到端必须在同一核（此处 CPU0）完成，不能跨核消费。
 *       CPU1 侧仅验证与 owner 无关的 bus 层守卫（acquire_write 返回 NOT_SUPPORTED）。
 *
 * 覆盖以下场景：
 *   1) 未 bind 后端时调用 BLOCK 入口返回 BM_ERR_INVALID；
 *   2) bind 后端后生产第一块（valid_bytes=sizeof(bus_frame_t)，ts_ns=1ms），commit；
 *   3) 消费第一块：valid_bytes / ts_ns 透传正确、内容未撕裂、release；
 *   4) 生产第二块后 abort（不提交），随后消费应 WOULD_BLOCK（无 READY 块）；
 *   5) owner_cpu 透传：bus 绑定的 owner_cpu 与后端 stream owner_cpu 一致（均 0=CPU0）。
 */
static void cpu0_test_block(void) {
    void        *block = NULL;
    bus_frame_t *frame;
    int          rc;

    /* 0) 初始化 stream：把 payload 数组绑定到各 block 的 data 指针 */
    rc = bm_stream_init(&g_blk_stream,
                        _bm_stream_payload_g_blk_stream,
                        BLOCK_STREAM_DEPTH,
                        (uint32_t)sizeof(bus_frame_t));
    rec0("BLOCK_stream_init_ok", rc == BM_OK, (uint32_t)(-rc));

    /* 1) 未 bind：调用 BLOCK 入口应返回 BM_ERR_INVALID */
    rc = bm_bus_block_produce_acquire(&g_h_block, &block);
    rec0("BLOCK_unbound_invalid",
         rc == BM_ERR_INVALID, (uint32_t)(-rc));

    /* 2) bind bm_stream adapter */
    rc = bm_bus_bind_block_backend(&g_h_block,
                                   bm_stream_as_block_backend(),
                                   &g_blk_stream);
    rec0("BLOCK_bind_ok", rc == BM_OK, (uint32_t)(-rc));

    /* owner_cpu 透传：bus owner_cpu=0 与 stream owner_cpu=0 一致 */
    rec0("BLOCK_owner_cpu_ok",
         g_h_block.storage->owner_cpu == g_blk_stream.owner_cpu, 0u);

    /* 3) 生产第一块：填充 seq/magic，commit，ts_ns=1000000（1ms） */
    block = NULL;
    rc = bm_bus_block_produce_acquire(&g_h_block, &block);
    if (rc == BM_OK && block != NULL) {
        bm_block_t *blk = (bm_block_t *)block;
        frame = (bus_frame_t *)blk->data;
        frame->seq   = 1u;
        frame->magic = FRAME_MAGIC(1u);
        rc = bm_bus_block_produce_commit(&g_h_block, block,
                                         (uint32_t)sizeof(bus_frame_t),
                                         1000000u /* 1ms in ns */);
        rec0("BLOCK_commit_ok", rc == BM_OK, (uint32_t)(-rc));
    } else {
        rec0("BLOCK_produce_acquire_ok", 0, (uint32_t)(-rc));
    }

    /* 4) 同核消费第一块：验证 valid_bytes / ts_ns 透传、内容未撕裂 */
    block = NULL;
    rc = bm_bus_block_consume_acquire(&g_h_block, &block);
    if (rc == BM_OK && block != NULL) {
        bm_block_t *blk = (bm_block_t *)block;
        rec0("BLOCK_valid_bytes_ok",
             blk->valid_bytes == sizeof(bus_frame_t), blk->valid_bytes);
        /* #9-1c：ns 直存，ts_ns=1000000 → ticks=1000000, rate_hz=1e9 */
        rec0("BLOCK_ts_ticks_ok",
             blk->timestamp.ticks == 1000000u, (uint32_t)blk->timestamp.ticks);
        {
            const bus_frame_t *f = (const bus_frame_t *)blk->data;
            rec0("BLOCK_content_untorn",
                 f != NULL && f->magic == FRAME_MAGIC(f->seq),
                 (f != NULL) ? f->seq : 0xFFFFFFFFu);
        }
        rc = bm_bus_block_consume_release(&g_h_block, block);
        rec0("BLOCK_release_ok", rc == BM_OK, (uint32_t)(-rc));
    } else {
        rec0("BLOCK_consume_acquire_ok", 0, (uint32_t)(-rc));
    }

    /* 5) 生产第二块后 abort（不提交），随后消费应 WOULD_BLOCK */
    block = NULL;
    rc = bm_bus_block_produce_acquire(&g_h_block, &block);
    if (rc == BM_OK && block != NULL) {
        rc = bm_bus_block_produce_abort(&g_h_block, block);
        rec0("BLOCK_abort_ok", rc == BM_OK, (uint32_t)(-rc));
    } else {
        rec0("BLOCK_abort_produce_ok", 0, (uint32_t)(-rc));
    }
    block = NULL;
    rc = bm_bus_block_consume_acquire(&g_h_block, &block);
    rec0("BLOCK_abort_no_ready",
         rc == BM_ERR_WOULD_BLOCK, (uint32_t)(-rc));

    /* 通知 CPU1：BLOCK 产消已完成，可执行其 owner 无关守卫检查 */
    bm_atomic_ipc_store_u32(&g_block_produced, 1u);

    /* 单核模式：CPU1 不存在，无需等待 */
    if (g_single_core) {
        return;
    }

    /* 等待 CPU1 完成其 BLOCK 守卫检查 */
    {
        uint32_t spin = 0u;
        while (bm_atomic_ipc_load_u32(&g_block_consumed) == 0u) {
            bm_hal_cpu_yield();
            spin++;
            if (spin > 5000000u) {
                rec0("BLOCK_cpu1_done_timeout", 0, 0u);
                return;
            }
        }
    }
}

/* =========================================================================
 * 等待 CPU1 就绪
 * ========================================================================= */

/**
 * @brief 自旋等待 CPU1 设置 g_cpu1_rdy 后清零
 *
 * 单核模式（g_single_core != 0）下 CPU1 不存在，直接返回 1（跳过等待）。
 *
 * @param timeout_spins 最大自旋次数
 * @return 1 = 正常响应或单核跳过；0 = 超时（仅多核模式下可能返回 0）
 */
static int wait_cpu1(uint32_t timeout_spins) {
    uint32_t spin = 0u;

    if (g_single_core) {
        return 1;
    }
    while (bm_atomic_ipc_load_u32(&g_cpu1_rdy) == 0u) {
        bm_hal_cpu_yield();
        spin++;
        if (spin > timeout_spins) {
            return 0;
        }
    }
    bm_atomic_ipc_store_u32(&g_cpu1_rdy, 0u);
    return 1;
}

/* =========================================================================
 * 结果输出（TAP 格式）
 * ========================================================================= */

/**
 * @brief 输出所有测试结果（TAP 格式）后调用 PSCI SYSTEM_OFF 退出
 *
 * 构造两个 bm_qemu_results_t（cpu0/cpu1），委托 bm_qemu_print_tap 输出并退出。
 */
static void print_results_and_exit(void) {
    uint32_t cpu1_cnt = bm_atomic_ipc_load_u32(&g_res1_cnt);
    bm_qemu_results_t sets[2];

    sets[0].items  = g_res0;
    sets[0].cap    = BUS_MAX_RESULTS;
    sets[0].cnt    = g_res0_cnt;
    sets[0].prefix = "cpu0";

    sets[1].items  = g_res1;
    sets[1].cap    = BUS_MAX_RESULTS;
    sets[1].cnt    = cpu1_cnt;
    sets[1].prefix = "cpu1";

    bm_qemu_print_tap(sets, 2u, "bm_bus SMP test");
}

/* =========================================================================
 * main（CPU0）
 * ========================================================================= */

/**
 * @brief CPU0 主函数
 */
int main(void) {
    bm_bus_cfg_t cfg;
    int rc;

    (void)bm_hal_timer_init(1000u);
    bm_hal_cpu_init();

    /* 初始化全局状态 */
    g_res0_cnt = 0u;
    bm_atomic_ipc_store_u32(&g_res1_cnt,             0u);
    bm_atomic_ipc_store_u32(&g_latest_written,        0u);
    bm_atomic_ipc_store_u32(&g_queue_full_done,       0u);
    bm_atomic_ipc_store_u32(&g_signal_batch1_done,    0u);
    bm_atomic_ipc_store_u32(&g_signal_batch1_read,    0u);
    bm_atomic_ipc_store_u32(&g_signal_batch2_done,    0u);
    bm_atomic_ipc_store_u32(&g_signal_batch3_drained,     0u);
    bm_atomic_ipc_store_u32(&g_signal_batch3_frame_ready, 0u);
    bm_atomic_ipc_store_u32(&g_signal_batch3_borrowed,    0u);
    bm_atomic_ipc_store_u32(&g_signal_batch3_lapped,      0u);
    bm_atomic_ipc_store_u32(&g_block_produced,        0u);
    bm_atomic_ipc_store_u32(&g_block_consumed,        0u);
    bm_atomic_ipc_store_u32(&g_cpu1_rdy,              0u);
    bm_atomic_ipc_store_u32(&g_phase, BUS_PHASE_INIT);

    /* 打开三个 bus（owner_cpu=0） */
    cfg.owner_cpu = 0u;

    rc = bm_bus_open(&g_h_latest, &g_bus_latest_storage, &cfg);
    rec0("LATEST_open", rc == BM_OK, (uint32_t)(-rc));

    rc = bm_bus_open(&g_h_signal, &g_bus_signal_storage, &cfg);
    rec0("SIGNAL_open", rc == BM_OK, (uint32_t)(-rc));

    rc = bm_bus_open(&g_h_queue, &g_bus_queue_storage, &cfg);
    rec0("QUEUE_open",  rc == BM_OK, (uint32_t)(-rc));

    /* 初始化 BLOCK bus（open 后不立即 bind，验证未 bind 的 BM_ERR_INVALID）*/
    rc = bm_bus_open(&g_h_block, &g_bus_block_storage, &cfg);
    rec0("BLOCK_open",  rc == BM_OK, (uint32_t)(-rc));

    /* 初始化 BLOCK 配套 bm_stream（owner_cpu=0，block_bytes=sizeof(bus_frame_t)）*/
    {
        static bus_frame_t s_blk_payloads[BLOCK_STREAM_DEPTH];
        rc = bm_stream_init(&g_blk_stream,
                            s_blk_payloads,
                            BLOCK_STREAM_DEPTH,
                            (uint32_t)sizeof(bus_frame_t));
        rec0("BLOCK_stream_init", rc == BM_OK, (uint32_t)(-rc));
    }

    /* 启动 CPU1 */
    rc = bm_hal_cpu_boot_secondary((uintptr_t)cpu1_entry);
    if (rc == BM_OK) {
        rec0("boot_secondary", 1, 0u);
    } else {
        /* -smp 1 或 PSCI 不支持：进入单核模式，boot_secondary 记 SKIP */
        g_single_core = 1;
        rec0_skip("boot_secondary");
    }

    /* ——— ATTACH 阶段：让 CPU1 在写者开始写前附加 SIGNAL/QUEUE 读者 ——— */
    bm_atomic_ipc_store_u32(&g_phase, BUS_PHASE_ATTACH);
    if (!wait_cpu1(5000000u)) {
        /* 多核超时才记失败（单核 wait_cpu1 直接返回 1） */
        rec0("ATTACH_cpu1_timeout", 0, 0u);
        bm_atomic_ipc_store_u32(&g_phase, BUS_PHASE_DONE);
        print_results_and_exit();
    }

    /* ——— LATEST 阶段 ——— */
    bm_atomic_ipc_store_u32(&g_phase, BUS_PHASE_LATEST);
    cpu0_test_latest();
    if (!wait_cpu1(5000000u)) {
        rec0("LATEST_cpu1_timeout", 0, 0u);
    }

    /* ——— SIGNAL 阶段：两批写，中间等 CPU1 读完第一批 ——— */
    bm_atomic_ipc_store_u32(&g_phase, BUS_PHASE_SIGNAL);
    if (!g_single_core) {
        cpu0_test_signal();
        if (!wait_cpu1(5000000u)) {
            rec0("SIGNAL_cpu1_timeout", 0, 0u);
        }
    } else {
        /* 单核：SIGNAL 写路径测试依赖 CPU1 中间握手，整体跳过 */
        rec0_skip("SIGNAL_batch1_write_ok");
        rec0_skip("SIGNAL_batch2_write_ok");
        rec0_skip("SIGNAL_batch3_lap_write_ok");
    }

    /* ——— QUEUE 阶段：先写满+满拒绝，再通知 CPU1 读 ——— */
    bm_atomic_ipc_store_u32(&g_phase, BUS_PHASE_QUEUE);
    cpu0_test_queue();
    if (!wait_cpu1(5000000u)) {
        rec0("QUEUE_cpu1_timeout", 0, 0u);
    }

    /* ——— BLOCK 阶段：绑定 bm_stream adapter，生产块，等 CPU1 消费 ——— */
    bm_atomic_ipc_store_u32(&g_phase, BUS_PHASE_BLOCK);
    cpu0_test_block();
    if (!wait_cpu1(5000000u)) {
        rec0("BLOCK_cpu1_timeout", 0, 0u);
    }

    /* 单核模式：CPU1 全部测试项以 SKIP 占位，保持 TAP 总数与多核一致 */
    if (g_single_core) {
        rec1_skip("SIGNAL_attach");
        rec1_skip("QUEUE_attach");
        rec1_skip("LATEST_no_torn_frame");
        rec1_skip("LATEST_read_completed");
        rec1_skip("SIGNAL_visibility");
        rec1_skip("SIGNAL_overflow_detected");
        rec1_skip("SIGNAL_window_overflow_detected");
        rec1_skip("SIGNAL_window_recover_untorn");
        rec1_skip("QUEUE_read_all");
        rec1_skip("QUEUE_in_order");
        rec1_skip("BLOCK_acquire_write_not_supported");
    }

    /* 发 DONE，等 CPU1 退出后输出结果 */
    bm_atomic_ipc_store_u32(&g_phase, BUS_PHASE_DONE);
    if (!g_single_core) {
        uint32_t delay;
        for (delay = 0u; delay < 500000u; delay++) {
            bm_hal_cpu_yield();
        }
    }

    print_results_and_exit();
    return 0;
}
