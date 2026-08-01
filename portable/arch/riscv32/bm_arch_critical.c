/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_critical.c
 * @brief RISC-V 32 位临界区实现（mstatus.MIE 保存/恢复）
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 * 2026-08-01       1.0            Codex           补齐 Doxygen 合规元数据
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 *
 */
#include "port/bm_arch_ops.h"
#include "riscv/common/bm_arch_riscv_mstatus.h"

bm_irq_state_t bm_arch_critical_enter(void) {
    bm_arch_riscv_word_t state = bm_arch_riscv_read_mstatus();
    bm_arch_riscv_clear_mie();
    return (bm_irq_state_t)state;
}

void bm_arch_critical_exit(bm_irq_state_t state) {
    bm_arch_riscv_write_mstatus((bm_arch_riscv_word_t)state);
}

int bm_arch_in_isr(void) {
    return bm_arch_riscv_in_isr();
}
