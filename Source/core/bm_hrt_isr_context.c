/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hrt_isr_context.c
 * @brief HRT 级 ISR 上下文标记（按核嵌套计数）
 *
 * 掩码模式（BM_CONFIG_ENABLE_PRIORITY_MASK=1）下，BM_CRITICAL_ENTER() 仅屏蔽
 * 低于 HRT 阈值的中断，HRT 级 ISR 与 SRT 路径不互斥；event/ultra/mempool 等
 * SRT 队列 API 依据本模块维护的按核深度计数对 HRT 级上下文 fail-closed。
 * 非掩码模式下计数照常维护但不影响行为，便于 Hardware HRT 端口统一接线。
 *
 * 计数安全性：ISR 入口硬件已屏蔽同级中断（Cortex-M 同优先级不可嵌套、
 * AArch64 IRQ 入口 PSTATE.I 置位、RISC-V mstatus.MIE 清零），同核 enter/exit
 * 天然平衡，故普通 volatile 读写即可，无需原子指令。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-31
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-30       1.0            zeh            正式发布
 * 2026-07-31       1.1            zeh            按核计数补 cache-line 隔离；
 *                                                CPU 越界改钳位，与 arch 层
 *                                                ISR 计数策略统一
 *
 */
#include "bm/common/bm_critical_wrap.h"
#include "bm/core/bm_cpu_local.h"
#include "bm_config.h"

/** 单核深度计数（仅本核读写，volatile 防编译器缓存） */
typedef struct {
    volatile uint32_t depth;
} bm_hrt_isr_depth_t;

/** 按核补齐到整 cache-line，防止多核相邻计数落在同一行造成伪共享 */
typedef BM_CACHE_LINE_PADDED_UNION(bm_hrt_isr_depth_t, slot,
                                   BM_CONFIG_CACHE_LINE) bm_hrt_isr_storage_t;

static BM_CACHE_ALIGNAS(BM_CONFIG_CACHE_LINE)
bm_hrt_isr_storage_t g_hrt_isr[BM_CONFIG_CPU_COUNT];

/**
 * @brief 取当前核的深度计数槽
 *
 * CPU 编号越界属配置错误（BM_CONFIG_CPU_COUNT 少于实际核数）。此处钳到界内
 * 而非忽略：忽略会使该核的 enter 不计数、bm_in_hrt_isr 恒假，等于静默关掉
 * 该核的 fail-closed 防护（fail-open）；钳位则至少保持 enter/exit/查询三者
 * 落在同一槽上，判定仍然自洽。与 portable/arch 层的 ISR 计数取同一策略。
 */
static bm_hrt_isr_depth_t *hrt_isr_this(void) {
    uint32_t cpu = bm_cpu_id();

    if (cpu >= BM_CONFIG_CPU_COUNT) {
        cpu = BM_CONFIG_CPU_COUNT - 1u;
    }
    return &g_hrt_isr[cpu].slot;
}

void bm_hrt_isr_enter(void) {
    hrt_isr_this()->depth++;
}

void bm_hrt_isr_exit(void) {
    bm_hrt_isr_depth_t *slot = hrt_isr_this();

    if (slot->depth > 0u) {
        slot->depth--;
    }
}

int bm_in_hrt_isr(void) {
    return (hrt_isr_this()->depth != 0u) ? 1 : 0;
}
