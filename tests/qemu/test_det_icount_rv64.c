/**
 * @file test_det_icount_rv64.c
 * @brief L2 QEMU icount 确定性——RISC-V64 virt 测试套件
 *
 * 覆盖 spec §5 L2 层（镜像 ARM 线 test_det_icount_arm.c，RV64 后端）：
 *   - probe 单调性 + overhead 标定（rdcycle CSR，已 de-risk 随执行推进）
 *   - clarke 纯函数 Δ==0（de-risk canary）
 *   - bus LATEST 同相位 round-trip Δ==0（双缓冲 2 相状态机，取同相位两遍）
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-07-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-01       0.1            zeh            RV64 L2 全套（probe/clarke/bus）
 *
 */
#include "bm_rv64_tap.h"
#define BM_ENABLE_PROBE 1
#include "bm_probe.h"
#include "bm/algorithm/bm_algo_motor.h"
#include "bm/core/bm_bus.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/** TAP 结果槽容量 */
#define DET_L2_RV64_RESULT_CAP 16u

/** @brief 全局 TAP 结果数组（静态分配，嵌入式无堆） */
static bm_qemu_result_t  g_results[DET_L2_RV64_RESULT_CAP];

/** @brief 测试套件结果集（前缀 "rv64" → TAP 输出 "rv64:name"） */
static bm_qemu_results_t g_rset = {
    g_results, DET_L2_RV64_RESULT_CAP, 0u, "rv64"
};

/** bus LATEST Δ 测试静态资源（文件作用域，零动态分配） */
BM_BUS_DEFINE(det_l2_rv64_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

/** @brief 骨架冒烟：套件可在 QEMU -icount 下引导运行 */
static void test_icount_boots(void)
{
    bm_qemu_record(&g_rset, "icount_boots", 1, 0u);
}

/**
 * @brief 探针单调性验证：两次读 a < b（rdcycle 在 icount 下单调递增）
 */
static void test_probe_monotonic(void)
{
    uint64_t a = bm_probe_cycles();
    __asm__ volatile("nop\nnop\nnop\nnop" ::: "memory");
    uint64_t b = bm_probe_cycles();
    bm_qemu_record(&g_rset, "probe_monotonic",
                   (b > a) ? 1 : 0,
                   (uint32_t)(b - a));
}

/**
 * @brief 探针开销打印：标定 overhead 并经 TAP 注释打印（不断言具体值）
 */
static void test_probe_overhead_print(void)
{
    uint64_t oh = bm_probe_overhead_calibrate();
    bm_rv64_uart_puts("# probe_overhead_cycles=");
    bm_rv64_uart_put_u32((uint32_t)oh);
    bm_rv64_uart_puts("\n");
    bm_qemu_record(&g_rset, "probe_overhead_print",
                   (oh < 1000u) ? 1 : 0,
                   (uint32_t)oh);
}

/**
 * @brief RV64 线 de-risk canary：clarke 纯函数同输入两遍 Δ 周期 == 0
 *
 * 纯函数无分支，指令序列固定，两遍执行周期应完全相同 → Δ==0。
 * 若 Δ≠0，icount/rdcycle 有问题，后续框架路径无意义（spec §5.2 闸 a）。
 */
static void test_clarke_delta_zero(void)
{
    volatile float      ia =  1.234f;
    volatile float      ib = -0.617f;
    volatile float      ic =  0.000f;
    bm_algo_abc_t       in;
    bm_algo_alphabeta_t out1;
    bm_algo_alphabeta_t out2;
    uint64_t            t0;
    uint64_t            t1;
    uint64_t            t2;
    uint64_t            t3;
    uint64_t            cyc1;
    uint64_t            cyc2;
    uint64_t            delta;

    in.ia = ia;
    in.ib = ib;
    in.ic = ic;

    memset(&out1, 0, sizeof(out1));
    t0 = bm_probe_cycles();
    bm_algo_clarke(&in, &out1);
    t1 = bm_probe_cycles();
    cyc1 = t1 - t0;

    memset(&out2, 0, sizeof(out2));
    t2 = bm_probe_cycles();
    bm_algo_clarke(&in, &out2);
    t3 = bm_probe_cycles();
    cyc2 = t3 - t2;

    delta = (cyc1 > cyc2) ? (cyc1 - cyc2) : (cyc2 - cyc1);

    bm_rv64_uart_puts("# clarke_cyc1=");
    bm_rv64_uart_put_u32((uint32_t)cyc1);
    bm_rv64_uart_puts(" cyc2=");
    bm_rv64_uart_put_u32((uint32_t)cyc2);
    bm_rv64_uart_puts(" delta=");
    bm_rv64_uart_put_u32((uint32_t)delta);
    bm_rv64_uart_puts("\n");

    bm_qemu_record(&g_rset, "clarke_delta_zero",
                   (delta == 0u) ? 1 : 0,
                   (uint32_t)delta);
}

/**
 * @brief RV64 线 bus LATEST 同相位 round-trip Δ 周期 == 0（框架路径控制流确定性）
 *
 * bus LATEST 是双缓冲+seqlock 的 2 相状态机（每 commit 交替 slot，周期=2）。
 * 取**同相位**两遍比较：预热定相 + 测第一遍 + 丢弃一遍推进相位 + 测第二遍。
 * 否则比较的是两个不同相位（slot 选择开销不同），Δ 稳定非零，非控制流非确定。
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

    (void)bm_bus_open(&bus, &det_l2_rv64_bus_storage, &cfg);
    (void)bm_bus_freeze(&bus);

    /* 预热：消除首次发布/首次读冷路径，令后续两遍稳态对称 */
    (void)bm_bus_acquire_write(&bus, &wslot);
    *(uint32_t *)wslot = 0xABCD1234u;
    (void)bm_bus_commit(&bus);
    readback = 0u;
    (void)bm_bus_latest_read(&bus, &readback);

    /* 稳态第一遍（相位 P） */
    t0 = bm_probe_cycles();
    (void)bm_bus_acquire_write(&bus, &wslot);
    *(uint32_t *)wslot = 0xABCD1234u;
    (void)bm_bus_commit(&bus);
    readback = 0u;
    (void)bm_bus_latest_read(&bus, &readback);
    t1 = bm_probe_cycles();
    cyc1 = t1 - t0;

    /* 丢弃一遍推进相位（周期=2），使被测两遍同相位 */
    (void)bm_bus_acquire_write(&bus, &wslot);
    *(uint32_t *)wslot = 0xABCD1234u;
    (void)bm_bus_commit(&bus);
    readback = 0u;
    (void)bm_bus_latest_read(&bus, &readback);

    /* 稳态第二遍（相位 P，与第一遍同相位） */
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

    bm_rv64_uart_puts("# bus_latest_cyc1=");
    bm_rv64_uart_put_u32((uint32_t)cyc1);
    bm_rv64_uart_puts(" cyc2=");
    bm_rv64_uart_put_u32((uint32_t)cyc2);
    bm_rv64_uart_puts(" delta=");
    bm_rv64_uart_put_u32((uint32_t)delta);
    bm_rv64_uart_puts("\n");

    bm_qemu_record(&g_rset, "bus_latest_delta_zero",
                   (delta == 0u) ? 1 : 0,
                   (uint32_t)delta);
}

/**
 * @brief 主入口：执行所有测试项，输出 TAP，经 sifive_test 退出 QEMU
 *
 * @return int 不会正常返回（sifive_test 退出后 QEMU 终止）
 */
int main(void)
{
    bm_probe_init();
    test_icount_boots();
    test_probe_monotonic();
    test_probe_overhead_print();
    test_clarke_delta_zero();
    test_bus_latest_delta_zero();
    bm_qemu_print_tap(&g_rset, 1u, "det_icount_rv64");
    return 0;
}
