/* SPDX-License-Identifier: GPL-3.0-or-later */
    .section .text.init
    .globl _start
    .type _start, @function
    .extern g_cpu0_system_init_done
    .extern g_secondary_mailbox
    .extern qemu_rv64_smp_secondary_start
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
    /* a0 = mhartid；先等主核完成 BSS 初始化 */
    la      t1, g_cpu0_system_init_done
.Lcpu0_init_wait:
    lw      t2, 0(t1)
    beqz    t2, .Lcpu0_init_wait

    /* 按 hart 索引 mailbox 自旋等待入口地址 */
    la      t1, g_secondary_mailbox
    slli    t2, a0, 3
    add     t1, t1, t2
2:
    fence   r, rw
    ld      t3, 0(t1)
    beqz    t3, 2b

    /* 按 hartid 计算独占栈顶：_estack_cpu0 - hartid * _stack_size_per_cpu */
    la sp, _estack_cpu0
    la t4, _stack_size_per_cpu
    mul t5, a0, t4
    sub sp, sp, t5

    mv a0, t3
    call qemu_rv64_smp_secondary_start
3:
    wfi
    j       3b

    .globl _trap_entry
    .type _trap_entry, @function
    .align 4
_trap_entry:
    addi    sp, sp, -112
    sd      ra, 0(sp)
    sd      t0, 8(sp)
    sd      t1, 16(sp)
    sd      t2, 24(sp)
    sd      a0, 32(sp)
    sd      a1, 40(sp)
    sd      a2, 48(sp)
    sd      a3, 56(sp)
    sd      a4, 64(sp)
    sd      a5, 72(sp)
    sd      a6, 80(sp)
    sd      a7, 88(sp)
    csrr    t0, mcause
    srli    t1, t0, 63
    beqz    t1, _trap_restore
    andi    t0, t0, 0xff
    li      t1, 7
    beq     t0, t1, _trap_timer
    li      t1, 3
    beq     t0, t1, _trap_software
    j       _trap_restore

_trap_timer:
    call    qemu_rv64_smp_on_timer_irq
    j       _trap_restore

_trap_software:
    call    qemu_rv64_smp_on_software_irq
    j       _trap_restore

_trap_restore:
    ld      a7, 88(sp)
    ld      a6, 80(sp)
    ld      a5, 72(sp)
    ld      a4, 64(sp)
    ld      a3, 56(sp)
    ld      a2, 48(sp)
    ld      a1, 40(sp)
    ld      a0, 32(sp)
    ld      t2, 24(sp)
    ld      t1, 16(sp)
    ld      t0, 8(sp)
    ld      ra, 0(sp)
    addi    sp, sp, 112
    mret
