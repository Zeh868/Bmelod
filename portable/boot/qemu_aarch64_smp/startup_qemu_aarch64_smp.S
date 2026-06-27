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
    bl      bm_aarch64_boot_init_vectors
    bl      SystemInit
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
    stp     x29, x30, [sp, #-16]!
    stp     x0, x1, [sp, #-16]!
    stp     x2, x3, [sp, #-16]!
    stp     x4, x5, [sp, #-16]!
    stp     x6, x7, [sp, #-16]!
    bl      bm_qemu_aarch64_irq_dispatch
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
    and     x0, x0, #0xFF
    ldr     x1, =_secondary_stack
    ldr     x2, =_secondary_stack_size
    add     sp, x1, x2
    bl      bm_aarch64_boot_init_vectors
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
