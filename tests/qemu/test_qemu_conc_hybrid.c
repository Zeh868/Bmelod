/**
 * @file test_qemu_conc_hybrid.c
 * @brief hybrid 层并发契约验证（stream owner 边界）QEMU Cortex-A15 SMP
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-26       1.0            zeh            T2.2 bm_stream owner 跨核拒绝契约
 *
 * 验证内容：
 *   - owner 正例（cpu0）：bm_stream_init / producer_acquire / commit 均返回 BM_OK
 *   - 跨核负例（cpu1）：producer_acquire 与 consumer_acquire 均被 stream_owner_valid
 *     拒绝（返回非 BM_OK），因为 BM_CPU_THIS()==1 != owner_cpu==0
 */
#include "bm_qemu_tap.h"
#include "bm/common/bm_atomic_ipc.h"
#include "bm/hybrid/bm_stream.h"
#include "hal/bm_hal_cpu.h"
#include "hal/bm_hal_timer.h"

#include <stdint.h>
#include <stddef.h>

/* ---- TAP 结果缓冲区 ---- */
#define HYB_MAX 16u

static bm_qemu_result_t  g_items0[HYB_MAX];
static bm_qemu_results_t g_res0 = { g_items0, HYB_MAX, 0u, "cpu0" };

static bm_qemu_result_t  g_items1[HYB_MAX];
static bm_qemu_results_t g_res1 = { g_items1, HYB_MAX, 0u, "cpu1" };
static bm_atomic_ipc_u32_t g_res1_cnt = BM_ATOMIC_IPC_U32_INIT(0u);

/**
 * @brief cpu1 侧结果记录（原子序号保护）
 */
static void rec1(const char *name, int pass, uint32_t info)
{
    uint32_t idx = bm_atomic_ipc_load_u32(&g_res1_cnt);
    if (idx < HYB_MAX) {
        g_items1[idx].name = name;
        g_items1[idx].pass = pass;
        g_items1[idx].info = info;
        bm_atomic_ipc_store_u32(&g_res1_cnt, idx + 1u);
    }
}

/* ---- stream 静态存储（owner_cpu 默认 0） ---- */
#define HYB_STREAM_DEPTH   4u
#define HYB_BLOCK_BYTES   64u

/** payload 后备存储 */
static uint8_t g_hyb_payloads[HYB_STREAM_DEPTH][HYB_BLOCK_BYTES];

/** 块描述符数组（BM_STREAM_INSTANCE 要求先声明） */
BM_STREAM_BLOCKS(g_hyb_stream, HYB_STREAM_DEPTH);

/** stream 实例（owner_cpu=0） */
BM_STREAM_INSTANCE(g_hyb_stream, HYB_STREAM_DEPTH);

/* ---- 双核阶段机 ---- */
typedef enum {
    HYB_INIT  = 0u,
    HYB_XCORE = 1u,
    HYB_DONE  = 9u
} hyb_phase_t;

static bm_atomic_ipc_u32_t g_phase       = BM_ATOMIC_IPC_U32_INIT(HYB_INIT);
static bm_atomic_ipc_u32_t g_cpu1_rdy    = BM_ATOMIC_IPC_U32_INIT(0u);

/**
 * @brief cpu1 入口：在非 owner 核调用 producer/consumer acquire，期望被拒
 *
 * stream 的 owner_cpu=0，此处 BM_CPU_THIS()==1，stream_owner_valid 应返回 0，
 * 因此两个 acquire 均应返回非 BM_OK。
 */
static void cpu1_entry(void)
{
    uint32_t phase;
    bm_block_t *blk = NULL;
    int rc_prod;
    int rc_cons;

    (void)bm_hal_timer_init(1000u);

    /* 等待主核发出 HYB_XCORE 信号 */
    do {
        phase = bm_atomic_ipc_load_u32(&g_phase);
        bm_hal_cpu_yield();
    } while (phase < HYB_XCORE);

    /* 跨核调用：均应被 stream_owner_valid 拒绝 */
    rc_prod = bm_stream_producer_acquire(&g_hyb_stream, &blk);
    rc_cons = bm_stream_consumer_acquire(&g_hyb_stream, &blk);

    rec1("stream_xcore_access_rejected",
         (rc_prod != BM_OK && rc_cons != BM_OK),
         (uint32_t)(-(rc_prod)));

    bm_atomic_ipc_store_u32(&g_cpu1_rdy, 1u);

    /* 等待主核完成 TAP 输出再自旋 */
    do {
        phase = bm_atomic_ipc_load_u32(&g_phase);
        bm_hal_cpu_yield();
    } while (phase < HYB_DONE);

    for (;;) { bm_hal_cpu_yield(); }
}

/**
 * @brief cpu0 主函数：owner 正例 + 协调 cpu1 跨核测试
 */
int main(void)
{
    int rc;
    bm_block_t *blk = NULL;
    bm_timestamp_t ts;
    uint32_t spin;

    (void)bm_hal_timer_init(1000u);
    bm_hal_cpu_init();

    /* --- T2.2 owner 正例：cpu0 == owner_cpu，操作应全部成功 --- */

    /* 零值时间戳（表示无时间戳） */
    ts.clock_id   = 0u;
    ts.quality    = 0u;
    ts.clock_epoch = 0u;
    ts.ticks      = 0u;
    ts.rate_hz    = 0u;

    rc = bm_stream_init(&g_hyb_stream,
                        (void *)g_hyb_payloads,
                        HYB_STREAM_DEPTH,
                        HYB_BLOCK_BYTES);
    bm_qemu_record(&g_res0, "stream_init_ok", rc == BM_OK, (uint32_t)(-(rc)));

    rc = bm_stream_producer_acquire(&g_hyb_stream, &blk);
    bm_qemu_record(&g_res0, "stream_owner_producer_ok", rc == BM_OK, (uint32_t)(-(rc)));

    if (rc == BM_OK) {
        (void)bm_stream_producer_commit(&g_hyb_stream, blk, 0u, &ts);
    }

    /* --- 启动 cpu1 --- */
    rc = bm_hal_cpu_boot_secondary((uintptr_t)cpu1_entry);
    bm_qemu_record(&g_res0, "boot_secondary", rc == BM_OK, (uint32_t)(-(rc)));

    /* 通知 cpu1 开始跨核测试 */
    bm_atomic_ipc_store_u32(&g_phase, HYB_XCORE);

    /* 等待 cpu1 完成（最多约 20M yield 周期） */
    spin = 0u;
    while (bm_atomic_ipc_load_u32(&g_cpu1_rdy) == 0u) {
        bm_hal_cpu_yield();
        if (++spin > 20000000u) { break; }
    }

    /* 通知 cpu1 可以自旋退出 */
    bm_atomic_ipc_store_u32(&g_phase, HYB_DONE);

    /* 收集 cpu1 结果并输出 TAP */
    g_res1.cnt = bm_atomic_ipc_load_u32(&g_res1_cnt);
    {
        bm_qemu_results_t sets[2];
        sets[0] = g_res0;
        sets[1] = g_res1;
        bm_qemu_print_tap(sets, 2u, "bm conc-hybrid test");
    }

    return 0;
}
