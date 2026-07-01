/**
 * @file bm_probe.h
 * @brief L2/L3 周期计数探针——ARM + RV64 双后端（测试期专用）
 *
 * 仅在 BM_ENABLE_PROBE 宏开启时编译（spec §8 探针污染红线）。
 * ARM 后端：ARMv7-A CNTVCT generic timer（CP15 mrrc p15,1,lo,hi,c14）。
 * RV64 后端：Task 8 追加 rdcycle（CSR 0xC00）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-07-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-01       0.1            zeh            Task 2：ARM CNTVCT 后端 + overhead 标定
 *
 */
#ifndef BM_PROBE_H
#define BM_PROBE_H

#ifdef BM_ENABLE_PROBE

#include <stdint.h>

#if defined(__ARM_ARCH) && (__ARM_ARCH >= 7) && !defined(__aarch64__)
/* ===== ARMv7-A Cortex-A15 后端：CNTVCT generic timer ===== */

/**
 * @brief 读取 ARMv7-A 虚拟计数器（CNTVCT）
 *
 * 使用 CP15 mrrc 指令读 64 位 generic timer 计数。
 * isb 确保之前的指令全部完成后才读取，防止乱序导致周期数不准。
 * 在 QEMU -icount 下，CNTVCT 随虚拟时间（=指令计数）确定性推进。
 *
 * @return 当前虚拟周期计数（64 位）
 */
static inline uint64_t bm_probe_cycles(void)
{
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile(
        "isb\n\t"
        "mrrc p15, 1, %0, %1, c14"
        : "=r"(lo), "=r"(hi)
        :
        : "memory"
    );
    return ((uint64_t)hi << 32u) | (uint64_t)lo;
}

#elif defined(__riscv) && (__riscv_xlen == 64)
/* ===== RISC-V64 后端：rdcycle CSR（Task 8 验证） ===== */

/**
 * @brief 读取 RISC-V64 周期计数器（rdcycle，CSR 0xC00）
 *
 * M-mode 裸机默认可读 mcycle。在 QEMU -icount 下随虚拟指令计数确定性推进。
 * 若此 QEMU 版本 rdcycle 不前进，Task 8 说明改用 CLINT mtime fallback。
 *
 * @return 当前周期计数（64 位）
 */
static inline uint64_t bm_probe_cycles(void)
{
    uint64_t v;
    __asm__ volatile("rdcycle %0" : "=r"(v));
    return v;
}

#else
#error "bm_probe.h: unsupported architecture (ARM ARMv7-A or RISC-V64 required)"
#endif /* arch */

/**
 * @brief 标定探针自身开销（两次连续读的周期差）
 *
 * 连续调用 bm_probe_cycles() 两次，返回差值作为"探针读取开销下界"。
 * 测量结果打印进 TAP 注释即可，不断言具体值（不同 QEMU 版本数值不同）。
 * 在计算被测区间净 Δ 时，可将此值从结果中参考性扣除（报告中注明）。
 *
 * @return 两次连续读的周期差（探针开销估计，cycles）
 */
static inline uint64_t bm_probe_overhead_calibrate(void)
{
    uint64_t a = bm_probe_cycles();
    uint64_t b = bm_probe_cycles();
    return b - a;
}

#endif /* BM_ENABLE_PROBE */
#endif /* BM_PROBE_H */
