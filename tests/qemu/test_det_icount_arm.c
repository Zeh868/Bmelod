/**
 * @file test_det_icount_arm.c
 * @brief L2 QEMU icount 确定性——ARM Cortex-A15 virt 测试套件
 *
 * 覆盖 spec §5 L2 层：
 *   - probe 单调性 + overhead 标定（bm_probe_overhead_calibrate）
 *   - clarke 纯函数 Δ==0（最早 de-risk，spec 5.2 闸 a）
 *   - bus LATEST round-trip Δ==0（框架路径）
 * 基线周期数打印到 UART，由 SOP 脚本存入 docs/superpowers/baselines/。
 *
 * Task 1 为骨架：仅含引导冒烟测试（icount_boots），后续 Task 2/3/4 补入
 * probe / clarke / bus 实测项。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-07-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-01       0.1            zeh            Task 1：骨架冒烟
 *
 */
#include "bm_qemu_tap.h"
#define BM_ENABLE_PROBE 1
#include "bm_probe.h"
#include "bm/algorithm/bm_algo_motor.h"
#include "bm/core/bm_bus.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/** bus LATEST Δ 测试静态资源（文件作用域，零动态分配） */
BM_BUS_DEFINE(det_l2_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

/** TAP 结果槽容量（为后续 Task 2/3/4 预留） */
#define DET_L2_ARM_RESULT_CAP 16u

/** @brief 全局 TAP 结果数组（静态分配，嵌入式无堆） */
static bm_qemu_result_t g_results[DET_L2_ARM_RESULT_CAP];

/** @brief 测试套件结果集（前缀 "arm" 对应 TAP 输出格式 "arm:name"） */
static bm_qemu_results_t g_rset = {
    g_results, DET_L2_ARM_RESULT_CAP, 0u, "arm"
};

/**
 * @brief 骨架冒烟：验证 icount 套件可在 QEMU -icount 模式下引导并运行
 *
 * Task 1 唯一测试项。后续 Task 2/3/4 在此函数之后追加测试调用。
 */
static void test_icount_boots(void)
{
    bm_qemu_record(&g_rset, "icount_boots", 1, 0u);
}

/**
 * @brief 探针单调性验证：两次读 a < b（PMCCNTR 在 icount 下单调递增）
 *
 * 若 b==a，说明 PMCCNTR 未随执行推进（未使能或 icount 配置错误），
 * 是后续所有周期测量的关键前置条件。
 */
static void test_probe_monotonic(void)
{
    uint64_t a = bm_probe_cycles();
    /* 插入几条 NOP 让时间前进一点 */
    __asm__ volatile("nop\nnop\nnop\nnop" ::: "memory");
    uint64_t b = bm_probe_cycles();
    bm_qemu_record(&g_rset, "probe_monotonic",
                   (b > a) ? 1 : 0,
                   (uint32_t)(b - a));
}

/**
 * @brief 探针开销打印：标定 overhead 并通过 TAP 注释打印（不断言具体值）
 *
 * overhead 绝对值因 QEMU 版本而异，仅作参考；打印进 TAP 注释，
 * 由 SOP 脚本抓取存入 docs/superpowers/baselines/。
 */
static void test_probe_overhead_print(void)
{
    uint64_t oh = bm_probe_overhead_calibrate();
    bm_qemu_uart_puts("# probe_overhead_cycles=");
    bm_qemu_uart_put_u32((uint32_t)oh);
    bm_qemu_uart_puts("\n");
    /* 只要 overhead < 1000 cycles 就认为合理（icount shift=0 下极小） */
    bm_qemu_record(&g_rset, "probe_overhead_print",
                   (oh < 1000u) ? 1 : 0,
                   (uint32_t)oh);
}

/**
 * @brief ARM 线 de-risk canary：clarke 纯函数同输入两遍 Δ 周期 == 0
 *
 * L2 icount 前提条件验证：纯函数无分支，指令序列固定，两遍执行指令数
 * 应完全相同 → Δ==0。若 Δ≠0，icount 有问题，后续框架路径测试无意义，
 * 立即红（spec §5.2 闸 a 的 canary）。测量窗口只括住 bm_algo_clarke() 本身，
 * 输入固定（volatile 防编译器优化掉调用）。
 */
static void test_clarke_delta_zero(void)
{
    volatile float ia =  1.234f;
    volatile float ib = -0.617f;
    volatile float ic =  0.000f;
    bm_algo_abc_t        in;
    bm_algo_alphabeta_t  out1;
    bm_algo_alphabeta_t  out2;
    uint64_t             t0;
    uint64_t             t1;
    uint64_t             t2;
    uint64_t             t3;
    uint64_t             cyc1;
    uint64_t             cyc2;
    uint64_t             delta;

    in.ia = ia;
    in.ib = ib;
    in.ic = ic;

    /* 第一遍：括住 clarke */
    memset(&out1, 0, sizeof(out1));
    t0 = bm_probe_cycles();
    bm_algo_clarke(&in, &out1);
    t1 = bm_probe_cycles();
    cyc1 = t1 - t0;

    /* 第二遍：相同输入 */
    memset(&out2, 0, sizeof(out2));
    t2 = bm_probe_cycles();
    bm_algo_clarke(&in, &out2);
    t3 = bm_probe_cycles();
    cyc2 = t3 - t2;

    /* Δ 周期 == 0（控制流确定性核心断言） */
    delta = (cyc1 > cyc2) ? (cyc1 - cyc2) : (cyc2 - cyc1);

    /* 打印周期数供基线记录 */
    bm_qemu_uart_puts("# clarke_cyc1=");
    bm_qemu_uart_put_u32((uint32_t)cyc1);
    bm_qemu_uart_puts(" cyc2=");
    bm_qemu_uart_put_u32((uint32_t)cyc2);
    bm_qemu_uart_puts(" delta=");
    bm_qemu_uart_put_u32((uint32_t)delta);
    bm_qemu_uart_puts("\n");

    /* 硬断言：Δ==0，不通过即控制流非确定 */
    bm_qemu_record(&g_rset, "clarke_delta_zero",
                   (delta == 0u) ? 1 : 0,
                   (uint32_t)delta);
}

/**
 * @brief ARM 线 bus LATEST round-trip Δ 周期 == 0（框架路径控制流确定性）
 *
 * 括住 acquire_write + 写入 + commit + latest_read 一次完整 round-trip，
 * 断言两遍稳态 round-trip 周期差 Δ==0（控制流确定）。
 *
 * 测量对象是**稳态** round-trip：控制环在已 open 的 bus 上反复 publish/read，
 * 而非每周期 open/close。故先做一次预热 round-trip 消除首次发布/首次读的冷
 * 路径分支（seqlock 从 NONE 起步等），再背靠背测两遍稳态，两遍状态严格对称。
 * 绝对周期数打印到 TAP 注释供基线记录。
 */
static void test_bus_latest_delta_zero(void)
{
    bm_bus_t      bus;
    bm_bus_cfg_t  cfg;
    void         *wslot;
    uint32_t      readback;
    uint64_t      t0;
    uint64_t      t1;
    uint64_t      t2;
    uint64_t      t3;
    uint64_t      cyc1;
    uint64_t      cyc2;
    uint64_t      delta;

    memset(&cfg, 0, sizeof(cfg));
    cfg.owner_cpu = 0u;

    (void)bm_bus_open(&bus, &det_l2_bus_storage, &cfg);
    (void)bm_bus_freeze(&bus);

    /* ---- 预热：消除首次发布/首次读的冷路径分支，令后续两遍稳态对称 ---- */
    (void)bm_bus_acquire_write(&bus, &wslot);
    *(uint32_t *)wslot = 0xABCD1234u;
    (void)bm_bus_commit(&bus);
    readback = 0u;
    (void)bm_bus_latest_read(&bus, &readback);

    /* ---- 稳态第一遍（相位 P） ---- */
    t0 = bm_probe_cycles();
    (void)bm_bus_acquire_write(&bus, &wslot);
    *(uint32_t *)wslot = 0xABCD1234u;
    (void)bm_bus_commit(&bus);
    readback = 0u;
    (void)bm_bus_latest_read(&bus, &readback);
    t1 = bm_probe_cycles();
    cyc1 = t1 - t0;

    /* ---- 丢弃一遍推进相位（LATEST 双缓冲每 commit 交替 slot，周期=2）----
     * 使被测两遍处于同一相位；否则比较的是状态机的两个不同相位（slot 选择
     * 开销不同），Δ 会稳定非零（实测差 7 周期），并非控制流非确定。 */
    (void)bm_bus_acquire_write(&bus, &wslot);
    *(uint32_t *)wslot = 0xABCD1234u;
    (void)bm_bus_commit(&bus);
    readback = 0u;
    (void)bm_bus_latest_read(&bus, &readback);

    /* ---- 稳态第二遍（相位 P，与第一遍同相位，严格对称） ---- */
    t2 = bm_probe_cycles();
    (void)bm_bus_acquire_write(&bus, &wslot);
    *(uint32_t *)wslot = 0xABCD1234u;
    (void)bm_bus_commit(&bus);
    readback = 0u;
    (void)bm_bus_latest_read(&bus, &readback);
    t3 = bm_probe_cycles();
    cyc2 = t3 - t2;

    (void)bm_bus_reset(&bus);
    (void)bm_bus_close(&bus);

    delta = (cyc1 > cyc2) ? (cyc1 - cyc2) : (cyc2 - cyc1);

    bm_qemu_uart_puts("# bus_latest_cyc1=");
    bm_qemu_uart_put_u32((uint32_t)cyc1);
    bm_qemu_uart_puts(" cyc2=");
    bm_qemu_uart_put_u32((uint32_t)cyc2);
    bm_qemu_uart_puts(" delta=");
    bm_qemu_uart_put_u32((uint32_t)delta);
    bm_qemu_uart_puts("\n");

    bm_qemu_record(&g_rset, "bus_latest_delta_zero",
                   (delta == 0u) ? 1 : 0,
                   (uint32_t)delta);
}

/**
 * @brief 主入口：执行所有测试项，输出 TAP，经 PSCI SYSTEM_OFF 退出 QEMU
 *
 * @return int 不会正常返回（PSCI 退出后 QEMU 终止）
 */
int main(void)
{
    bm_probe_init();
    test_icount_boots();
    test_probe_monotonic();
    test_probe_overhead_print();
    test_clarke_delta_zero();
    test_bus_latest_delta_zero();
    bm_qemu_print_tap(&g_rset, 1u, "det_icount_arm");
    /* 不会执行到这里：bm_qemu_print_tap 内部调用 bm_qemu_exit() */
    return 0;
}
