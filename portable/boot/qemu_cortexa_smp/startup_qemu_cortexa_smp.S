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
    ldr     r1, =_secondary_stack
    ldr     r2, =_secondary_stack_size
    add     sp, r1, r2
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
