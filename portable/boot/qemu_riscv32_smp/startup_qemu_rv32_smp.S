/* SPDX-License-Identifier: GPL-3.0-or-later */
    .section .text.init
    .globl _start
    .type _start, @function
    .extern g_cpu0_system_init_done
    .extern g_secondary_mailbox
    .extern qemu_rv32_smp_secondary_start
_start:
    /* 所有 hart：关 MIE 并安装 M-mode trap 向量 */
    csrci mstatus, 8
    la t0, _trap_entry
    csrw mtvec, t0
    csrr a0, mhartid
    bnez a0, secondary_park

    /* 主核：设栈、初始化并进入 main */
    la sp, _estack_cpu0
    call SystemInit
    call main
1:
    j 1b

secondary_park:
    /* a0 = mhartid；先等主核完成 BSS 初始化（对齐 RV64 SMP 实现） */
    la t1, g_cpu0_system_init_done
.Lcpu0_init_wait:
    lw t2, 0(t1)
    beqz t2, .Lcpu0_init_wait

    /* 按 hart 索引 mailbox 自旋等待入口地址 */
    la t1, g_secondary_mailbox
    slli t2, a0, 2
    add t1, t1, t2
2:
    fence r, rw
    lw t3, 0(t1)
    beqz t3, 2b

    /* 按 hartid 计算独占栈顶：_estack_cpu0 - hartid * _stack_size_per_cpu */
    la sp, _estack_cpu0
    la t4, _stack_size_per_cpu
    mul t5, a0, t4
    sub sp, sp, t5

    mv a0, t3
    call qemu_rv32_smp_secondary_start
3:
    j 3b

    .globl _trap_entry
    .type _trap_entry, @function
    .align 4
_trap_entry:
    /* 保存全部 caller-saved 寄存器（C ABI：ra/t0-t6/a0-a7），对齐 RV64 SMP 实现 */
    addi    sp, sp, -64
    sw      ra, 0(sp)
    sw      t0, 4(sp)
    sw      t1, 8(sp)
    sw      t2, 12(sp)
    sw      t3, 16(sp)
    sw      t4, 20(sp)
    sw      t5, 24(sp)
    sw      t6, 28(sp)
    sw      a0, 32(sp)
    sw      a1, 36(sp)
    sw      a2, 40(sp)
    sw      a3, 44(sp)
    sw      a4, 48(sp)
    sw      a5, 52(sp)
    sw      a6, 56(sp)
    sw      a7, 60(sp)
    csrr    t0, mcause
    srli    t1, t0, 31
    beqz    t1, _trap_restore
    andi    t0, t0, 0xff
    li      t1, 7
    beq     t0, t1, _trap_timer
    li      t1, 3
    beq     t0, t1, _trap_software
    j       _trap_restore

_trap_timer:
    call    qemu_rv32_smp_on_timer_irq
    j       _trap_restore

_trap_software:
    call    qemu_rv32_smp_on_software_irq
    j       _trap_restore

_trap_restore:
    /* mret 前清 mcause：否则符号位残留使 bm_arch_riscv_in_isr() 线程态永久误报 1 */
    csrw    mcause, zero
    lw      a7, 60(sp)
    lw      a6, 56(sp)
    lw      a5, 52(sp)
    lw      a4, 48(sp)
    lw      a3, 44(sp)
    lw      a2, 40(sp)
    lw      a1, 36(sp)
    lw      a0, 32(sp)
    lw      t6, 28(sp)
    lw      t5, 24(sp)
    lw      t4, 20(sp)
    lw      t3, 16(sp)
    lw      t2, 12(sp)
    lw      t1, 8(sp)
    lw      t0, 4(sp)
    lw      ra, 0(sp)
    addi    sp, sp, 64
    mret
