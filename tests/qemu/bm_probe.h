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
/* ===== ARMv7-A Cortex-A15 后端：PMU PMCCNTR 周期计数器 =====
 *
 * 关键选型：不用 CNTVCT（generic timer）。实测 QEMU -icount 下 CNTVCT 按
 * 翻译块（TB）边界记账，读数不随执行指令数变化（注入 65 条 nop 仍 Δ==0），
 * 会把 Δ==0 断言退化为恒真的空测试。PMCCNTR 在 icount 下随执行确定性推进，
 * 提供指令/周期级分辨率，才能让 Δ==0 成为真正的控制流确定性证据。
 */

/**
 * @brief 使能 PMU 周期计数器（PMCCNTR），测量前调用一次
 *
 * 在 EL1（裸机特权态）配置 ARMv7-A 性能监视单元：
 *   - PMCR.E(bit0)=1 使能计数、PMCR.C(bit2)=1 复位 CCNT、PMCR.D(bit3)=0 每周期计数（不 /64）；
 *   - PMCNTENSET.C(bit31)=1 使能周期计数器。
 */
static inline void bm_probe_init(void)
{
    uint32_t pmcr;
    __asm__ volatile("mrc p15, 0, %0, c9, c12, 0" : "=r"(pmcr));
    pmcr |= (1u << 0) | (1u << 2);   /* E: 使能；C: 复位 CCNT */
    pmcr &= ~(1u << 3);              /* D=0: 每周期计数，不 /64 */
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 0" :: "r"(pmcr));
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 1" :: "r"(0x80000000u)); /* PMCNTENSET.C */
    __asm__ volatile("isb");
}

/**
 * @brief 读取 ARMv7-A 周期计数器（PMCCNTR，CP15 c9,c13,0）
 *
 * isb 序列化，确保之前指令全部完成后再采样，防止乱序导致周期数不准。
 * 在 QEMU -icount 下 PMCCNTR 随执行指令确定性推进（32 位，短测量足够）。
 *
 * @return 当前周期计数（低 32 位零扩展到 64 位）
 */
static inline uint64_t bm_probe_cycles(void)
{
    uint32_t c;
    __asm__ volatile(
        "isb\n\t"
        "mrc p15, 0, %0, c9, c13, 0"
        : "=r"(c)
        :
        : "memory"
    );
    return (uint64_t)c;
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

/**
 * @brief RV64 探针初始化（rdcycle 在 M-mode 默认可用，无需使能，置空占位）
 *
 * 与 ARM 后端保持 API 对称，测试源可统一调用 bm_probe_init()。
 */
static inline void bm_probe_init(void) { }

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
