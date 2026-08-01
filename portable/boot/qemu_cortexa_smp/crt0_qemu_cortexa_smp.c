/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file crt0_qemu_cortexa_smp.c
 * @brief QEMU ARMv7-A virt SMP 启动：复制 .data 并清零 .bss
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-16
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            正式发布
 * 2026-07-16       1.1            zeh            新增 g_cortexa_irq_stacks 每核 IRQ 模式栈（startup 汇编初始化 banked SP_irq，修复首个 GIC 中断 stmfd 写野地址）
 * 2026-08-01       1.1            zeh           补齐 Doxygen 合规元数据
 *
 */
#include "bm_config.h"

#include <stdint.h>

extern uintptr_t _sidata;
extern uintptr_t _sdata;
extern uintptr_t _edata;
extern uintptr_t _sbss;
extern uintptr_t _ebss;

/**
 * @brief 每核 IRQ 模式栈（startup 汇编初始化 banked SP_irq 时引用）
 *
 * ARMv7-A 的 IRQ 模式使用 banked SP_irq，复位后无有效值；
 * 首个 GIC 中断的 stmfd 会写野地址。每核 1024B：容纳
 * cortexa_irq_handler 的 r0-r12+lr 现场（56B）+ C 分发与较重回调链帧。
 * 注：startup 汇编中的每核步长字面量须与本数组保持一致。
 */
__attribute__((aligned(8)))
uint8_t g_cortexa_irq_stacks[BM_CONFIG_CPU_COUNT * 1024u];

void SystemInit(void) {
    uintptr_t *src = (uintptr_t *)&_sidata;
    uintptr_t *dst = (uintptr_t *)&_sdata;

    while (dst < (uintptr_t *)&_edata) {
        *dst++ = *src++;
    }

    dst = (uintptr_t *)&_sbss;
    while (dst < (uintptr_t *)&_ebss) {
        *dst++ = 0;
    }
}
