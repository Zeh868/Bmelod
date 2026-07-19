/* SPDX-License-Identifier: GPL-3.0-or-later */
    .arm
    .cpu cortex-a15
    .fpu vfpv3-d16

    .section .text.init, "ax", %progbits
    .globl  _vector_table
_vector_table:
    ldr     pc, =_start
    ldr     pc, =cortexa_undef_handler
    ldr     pc, =cortexa_svc_handler
    ldr     pc, =cortexa_prefetch_abort_handler
    ldr     pc, =cortexa_data_abort_handler
    nop
    ldr     pc, =cortexa_irq_handler
    ldr     pc, =cortexa_fiq_handler

    .globl  _start
    .type   _start, %function
_start:
    mrc     p15, 0, r0, c0, c0, 5
    ands    r0, r0, #3
    bne     cortexa_secondary_park

    /* 初始化 IRQ 模式栈（SP_irq 为 banked 寄存器，每核须各自设置，
       否则首个 GIC 中断的 stmfd 写野地址；步长 1024B 与
       crt0 g_cortexa_irq_stacks 一致） */
    mrs     r1, cpsr
    bic     r2, r1, #0x1f
    orr     r2, r2, #0x12
    msr     cpsr_c, r2
    ldr     r2, =g_cortexa_irq_stacks
    mov     r3, #1024
    mul     r4, r0, r3
    add     r2, r2, r4
    add     sp, r2, r3
    msr     cpsr_c, r1

    ldr     sp, =_estack
    bl      SystemInit
    bl      main
1:
    wfi
    b       1b

cortexa_secondary_park:
2:
    wfe
    b       2b

    .macro  cortexa_panic_loop label
    .type   \label, %function
\label:
1:
    wfi
    b       1b
    .endm

cortexa_undef_handler:          cortexa_panic_loop cortexa_undef_handler
cortexa_svc_handler:            cortexa_panic_loop cortexa_svc_handler
cortexa_prefetch_abort_handler: cortexa_panic_loop cortexa_prefetch_abort_handler
cortexa_data_abort_handler:     cortexa_panic_loop cortexa_data_abort_handler
cortexa_fiq_handler:            cortexa_panic_loop cortexa_fiq_handler

    .type   cortexa_irq_handler, %function
cortexa_irq_handler:
    sub     lr, lr, #4
    stmfd   sp!, {r0-r12, lr}
    bl      bm_qemu_cortexa_irq_dispatch
    ldmfd   sp!, {r0-r12, pc}^

    .section .text
    .globl  bm_cortexa_secondary_startup
    .type   bm_cortexa_secondary_startup, %function
bm_cortexa_secondary_startup:
    mrc     p15, 0, r0, c0, c0, 5
    and     r0, r0, #3
    /* 同 _start：先初始化本核 IRQ 模式栈再进 C（步长 1024B 同 crt0） */
    mrs     r1, cpsr
    bic     r2, r1, #0x1f
    orr     r2, r2, #0x12
    msr     cpsr_c, r2
    ldr     r2, =g_cortexa_irq_stacks
    mov     r3, #1024
    mul     r4, r0, r3
    add     r2, r2, r4
    add     sp, r2, r3
    msr     cpsr_c, r1
    ldr     r1, =_secondary_stack
    ldr     r2, =_secondary_stack_size_per_cpu
    mul     r3, r0, r2
    add     r3, r3, r2
    add     sp, r1, r3
    bl      bm_cortexa_secondary_entry_c
3:
    wfi
    b       3b

    .globl  bm_cortexa_boot_init_vectors
    .type   bm_cortexa_boot_init_vectors, %function
bm_cortexa_boot_init_vectors:
    ldr     r0, =_vector_table
    mcr     p15, 0, r0, c12, c0, 0
    dsb
    isb
    bx      lr
