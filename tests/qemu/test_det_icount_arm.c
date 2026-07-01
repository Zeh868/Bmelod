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
#include <stdint.h>
#include <stddef.h>

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
 * @brief 主入口：执行所有测试项，输出 TAP，经 PSCI SYSTEM_OFF 退出 QEMU
 *
 * @return int 不会正常返回（PSCI 退出后 QEMU 终止）
 */
int main(void)
{
    test_icount_boots();
    bm_qemu_print_tap(&g_rset, 1u, "det_icount_arm");
    /* 不会执行到这里：bm_qemu_print_tap 内部调用 bm_qemu_exit() */
    return 0;
}
