/**
 * @file bm_arch_critical.c
 * @brief RISC-V 32 位临界区实现（mstatus.MIE 保存/恢复）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
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
