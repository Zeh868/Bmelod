/* SPDX-License-Identifier: GPL-3.0-or-later */
    .section .text.init
    .globl _start
    .type _start, %function
_start:
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF
    cbz     x0, 1f
2:
    wfe
    b       2b
1:
    ldr     x0, =_estack
    mov     sp, x0
    /* 使能 EL1 浮点/SIMD（CPACR_EL1.FPEN=b11 不陷入；QEMU 复位默认陷入，
       框架算法库含浮点代码，不开则首个浮点指令即 sync 异常） */
    mrs     x0, cpacr_el1
    orr     x0, x0, #(3 << 20)
    msr     cpacr_el1, x0
    isb
    bl      bm_aarch64_boot_init_vectors
    bl      SystemInit
    /* 复位 PSTATE.DAIF 全屏蔽：开 IRQ 让 Generic Timer PPI 得以送达
       （GIC 分发器此时尚未使能，提前开无挂起风险） */
    msr     daifclr, #2
    bl      main
3:
    wfi
    b       3b

    .section .text.vectors, "ax", %progbits
    .align  11
    .globl  aarch64_vector_table
aarch64_vector_table:
    .align  7
    b       aarch64_sync_sp0
    .align  7
    b       aarch64_irq_sp0
    .align  7
    b       aarch64_fiq_sp0
    .align  7
    b       aarch64_serr_sp0
    .align  7
    b       aarch64_sync_spx
    .align  7
    b       aarch64_irq_spx
    .align  7
    b       aarch64_fiq_spx
    .align  7
    b       aarch64_serr_spx
    .align  7
    b       aarch64_sync_el0_64
    .align  7
    b       aarch64_irq_el0_64
    .align  7
    b       aarch64_fiq_el0_64
    .align  7
    b       aarch64_serr_el0_64
    .align  7
    b       aarch64_sync_el0_32
    .align  7
    b       aarch64_irq_el0_32
    .align  7
    b       aarch64_fiq_el0_32
    .align  7
    b       aarch64_serr_el0_32

    .macro  aarch64_vector_panic
    .align  7
    wfi
    b       .
    .endm

aarch64_sync_sp0:       aarch64_vector_panic
aarch64_irq_sp0:        aarch64_vector_panic
aarch64_fiq_sp0:        aarch64_vector_panic
aarch64_serr_sp0:       aarch64_vector_panic
aarch64_sync_spx:       aarch64_vector_panic
aarch64_fiq_spx:        aarch64_vector_panic
aarch64_serr_spx:       aarch64_vector_panic
aarch64_sync_el0_64:    aarch64_vector_panic
aarch64_irq_el0_64:     aarch64_vector_panic
aarch64_fiq_el0_64:     aarch64_vector_panic
aarch64_serr_el0_64:    aarch64_vector_panic
aarch64_sync_el0_32:    aarch64_vector_panic
aarch64_irq_el0_32:     aarch64_vector_panic
aarch64_fiq_el0_32:     aarch64_vector_panic
aarch64_serr_el0_32:    aarch64_vector_panic

aarch64_irq_spx:
    /* 保存全部 caller-saved 通用寄存器（x0-x18 + x29/x30；x19 仅为凑对保持
       16B 对齐顺带保存） */
    stp     x29, x30, [sp, #-16]!
    stp     x0, x1, [sp, #-16]!
    stp     x2, x3, [sp, #-16]!
    stp     x4, x5, [sp, #-16]!
    stp     x6, x7, [sp, #-16]!
    stp     x8, x9, [sp, #-16]!
    stp     x10, x11, [sp, #-16]!
    stp     x12, x13, [sp, #-16]!
    stp     x14, x15, [sp, #-16]!
    stp     x16, x17, [sp, #-16]!
    stp     x18, x19, [sp, #-16]!
    /* 保存 SIMD/FP 现场 q0-q31 + FPCR/FPSR：C 分发可能触达浮点回调 */
    sub     sp, sp, #512
    stp     q0, q1, [sp]
    stp     q2, q3, [sp, #32]
    stp     q4, q5, [sp, #64]
    stp     q6, q7, [sp, #96]
    stp     q8, q9, [sp, #128]
    stp     q10, q11, [sp, #160]
    stp     q12, q13, [sp, #192]
    stp     q14, q15, [sp, #224]
    stp     q16, q17, [sp, #256]
    stp     q18, q19, [sp, #288]
    stp     q20, q21, [sp, #320]
    stp     q22, q23, [sp, #352]
    stp     q24, q25, [sp, #384]
    stp     q26, q27, [sp, #416]
    stp     q28, q29, [sp, #448]
    stp     q30, q31, [sp, #480]
    mrs     x0, fpcr
    mrs     x1, fpsr
    stp     x0, x1, [sp, #-16]!
    bl      bm_qemu_aarch64_irq_dispatch
    ldp     x0, x1, [sp], #16
    msr     fpcr, x0
    msr     fpsr, x1
    ldp     q0, q1, [sp]
    ldp     q2, q3, [sp, #32]
    ldp     q4, q5, [sp, #64]
    ldp     q6, q7, [sp, #96]
    ldp     q8, q9, [sp, #128]
    ldp     q10, q11, [sp, #160]
    ldp     q12, q13, [sp, #192]
    ldp     q14, q15, [sp, #224]
    ldp     q16, q17, [sp, #256]
    ldp     q18, q19, [sp, #288]
    ldp     q20, q21, [sp, #320]
    ldp     q22, q23, [sp, #352]
    ldp     q24, q25, [sp, #384]
    ldp     q26, q27, [sp, #416]
    ldp     q28, q29, [sp, #448]
    ldp     q30, q31, [sp, #480]
    add     sp, sp, #512
    ldp     x18, x19, [sp], #16
    ldp     x16, x17, [sp], #16
    ldp     x14, x15, [sp], #16
    ldp     x12, x13, [sp], #16
    ldp     x10, x11, [sp], #16
    ldp     x8, x9, [sp], #16
    ldp     x6, x7, [sp], #16
    ldp     x4, x5, [sp], #16
    ldp     x2, x3, [sp], #16
    ldp     x0, x1, [sp], #16
    ldp     x29, x30, [sp], #16
    eret

    .section .text
    .globl  bm_aarch64_secondary_startup
    .type   bm_aarch64_secondary_startup, %function
bm_aarch64_secondary_startup:
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF        /* x0 = CPU ID（QEMU virt 单 cluster） */
    /* 按 CPU ID 计算独占栈顶：_secondary_stack + (cpu_id + 1) * stack_size */
    ldr     x1, =_secondary_stack
    ldr     x2, =_secondary_stack_size_per_cpu
    mul     x3, x0, x2
    add     x3, x3, x2
    add     sp, x1, x3
    /* 同主核：使能 EL1 浮点/SIMD */
    mrs     x0, cpacr_el1
    orr     x0, x0, #(3 << 20)
    msr     cpacr_el1, x0
    isb
    bl      bm_aarch64_boot_init_vectors
    /* 同主核：开 IRQ（复位 DAIF 全屏蔽） */
    msr     daifclr, #2
    bl      bm_aarch64_secondary_entry_c
1:
    wfi
    b       1b

    .section .text.boot
    .globl  bm_aarch64_boot_init_vectors
    .type   bm_aarch64_boot_init_vectors, %function
bm_aarch64_boot_init_vectors:
    adr     x0, aarch64_vector_table
    msr     vbar_el1, x0
    isb
    ret
