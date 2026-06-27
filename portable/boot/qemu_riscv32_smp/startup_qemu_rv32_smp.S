/* SPDX-License-Identifier: GPL-3.0-or-later */
    .section .text.init
    .globl _start
    .type _start, @function
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
    /* a0 = mhartid；按 hart 索引 mailbox 自旋等待入口地址 */
    la t1, g_secondary_mailbox
    slli t2, a0, 2
    add t1, t1, t2
2:
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
_trap_entry:
    csrr t0, mcause
    srli t1, t0, 31
    beqz t1, _trap_done
    andi t0, t0, 0xff
    li t1, 7
    beq t0, t1, _trap_timer
    li t1, 3
    beq t0, t1, _trap_software
    j _trap_done

_trap_timer:
    call qemu_rv32_smp_on_timer_irq
    j _trap_done

_trap_software:
    call qemu_rv32_smp_on_software_irq
    j _trap_done

_trap_done:
    mret
