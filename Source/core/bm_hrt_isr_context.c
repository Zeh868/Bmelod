/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hrt_isr_context.c
 * @brief HRT 级 ISR 上下文标记（按核嵌套计数）
 *
 * 掩码模式（BM_CONFIG_ENABLE_PRIORITY_MASK=1）下，BM_CRITICAL_ENTER() 仅屏蔽
 * 低于 HRT 阈值的中断，HRT 级 ISR 与 SRT 路径不互斥；event/mempool 等 SRT
 * 队列 API 依据本模块维护的按核深度计数对 HRT 级上下文 fail-closed。
 * 非掩码模式下计数照常维护但不影响行为，便于 Hardware HRT 端口统一接线。
 *
 * 计数安全性：ISR 入口硬件已屏蔽同级中断（Cortex-M 同优先级不可嵌套、
 * AArch64 IRQ 入口 PSTATE.I 置位、RISC-V mstatus.MIE 清零），同核 enter/exit
 * 天然平衡，故普通 volatile 读写即可，无需原子指令。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-30
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-30       1.0            zeh            正式发布
 *
 */
#include "bm/common/bm_critical_wrap.h"
#include "bm/common/bm_cpu_local.h"
#include "bm_config.h"

/** 按核 HRT ISR 嵌套深度；仅本核读写，volatile 防编译器缓存 */
static volatile uint32_t g_hrt_isr_depth[BM_CONFIG_CPU_COUNT];

void bm_hrt_isr_enter(void) {
    uint32_t cpu = bm_cpu_id();

    if (cpu < BM_CONFIG_CPU_COUNT) {
        g_hrt_isr_depth[cpu]++;
    }
}

void bm_hrt_isr_exit(void) {
    uint32_t cpu = bm_cpu_id();

    if (cpu < BM_CONFIG_CPU_COUNT && g_hrt_isr_depth[cpu] > 0u) {
        g_hrt_isr_depth[cpu]--;
    }
}

int bm_in_hrt_isr(void) {
    uint32_t cpu = bm_cpu_id();

    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return 0;
    }
    return (g_hrt_isr_depth[cpu] != 0u) ? 1 : 0;
}
