/**
 * @file vectors_qemu_esp32_smp.S
 * @brief QEMU ESP32 Xtensa SMP Level1 中断向量与 TIMG0 T0 分发
 *
 * 向量布局对齐 ESP32 XEA2 VECBASE 偏移；UserException 处理 Level1 外设中断。
 */
    .extern qemu_esp32_smp_on_timer_irq

    .section .iram1.vectors, "ax"
    .align 8
    .global _qemu_esp32_vector_base
_qemu_esp32_vector_base:

/* WindowOverflow4 @ 0x00 */
    .org _qemu_esp32_vector_base + 0x00
    .global _WindowOverflow4
_WindowOverflow4:
    s32e    a0, a5, -16
    s32e    a1, a5, -12
    s32e    a2, a5, -8
    s32e    a3, a5, -4
    rfwo

/* WindowUnderflow4 @ 0x40 */
    .org _qemu_esp32_vector_base + 0x40
    .global _WindowUnderflow4
_WindowUnderflow4:
    l32e    a0, a5, -16
    l32e    a1, a5, -12
    l32e    a2, a5, -8
    l32e    a3, a5, -4
    rfwu

/* WindowOverflow8 @ 0x80 */
    .org _qemu_esp32_vector_base + 0x80
    .global _WindowOverflow8
_WindowOverflow8:
    s32e    a0, a9, -16
    l32e    a0, a1, -12
    s32e    a1, a9, -12
    s32e    a2, a9, -8
    s32e    a3, a9, -4
    s32e    a4, a0, -32
    s32e    a5, a0, -28
    s32e    a6, a0, -24
    s32e    a7, a0, -20
    rfwo

/* WindowUnderflow8 @ 0xC0 */
    .org _qemu_esp32_vector_base + 0xC0
    .global _WindowUnderflow8
_WindowUnderflow8:
    l32e    a0, a9, -16
    l32e    a1, a9, -12
    l32e    a2, a9, -8
    l32e    a3, a9, -4
    l32e    a4, a9, -32
    l32e    a5, a9, -28
    l32e    a6, a9, -24
    l32e    a7, a9, -20
    rfwu

/* WindowOverflow12 @ 0x100 */
    .org _qemu_esp32_vector_base + 0x100
    .global _WindowOverflow12
_WindowOverflow12:
    s32e    a0, a9, -16
    l32e    a0, a1, -12
    s32e    a1, a9, -12
    s32e    a2, a9, -8
    s32e    a3, a9, -4
    s32e    a4, a9, -32
    s32e    a5, a9, -28
    s32e    a6, a9, -24
    s32e    a7, a9, -20
    s32e    a8, a0, -48
    s32e    a9, a0, -44
    s32e    a10, a0, -40
    s32e    a11, a0, -36
    rfwo

/* WindowUnderflow12 @ 0x140 */
    .org _qemu_esp32_vector_base + 0x140
    .global _WindowUnderflow12
_WindowUnderflow12:
    l32e    a0, a9, -16
    l32e    a1, a9, -12
    l32e    a2, a9, -8
    l32e    a3, a9, -4
    l32e    a4, a9, -32
    l32e    a5, a9, -28
    l32e    a6, a9, -24
    l32e    a7, a9, -20
    l32e    a8, a9, -48
    l32e    a9, a9, -44
    l32e    a10, a9, -40
    l32e    a11, a9, -36
    rfwu

/* Level2..5 / Debug / NMI / Kernel：未使用则自旋 */
    .org _qemu_esp32_vector_base + 0x180
1:
    waiti   0
    j       1b

    .org _qemu_esp32_vector_base + 0x300
2:
    waiti   0
    j       2b

    .org _qemu_esp32_vector_base + 0x3C0
3:
    waiti   0
    j       3b

    .org _qemu_esp32_vector_base + 0x400
4:
    waiti   0
    j       4b

/* UserException @ 0x410：Level1 中断入口 */
    .org _qemu_esp32_vector_base + 0x410
    .global _UserExceptionVector
_UserExceptionVector:
    wsr     a0, excsave1
    call0   _qemu_esp32_user_exc

    .org _qemu_esp32_vector_base + 0x470
5:
    waiti   0
    j       5b

    .section .iram1.text, "ax"
    .align 4
    .type _qemu_esp32_user_exc, @function
_qemu_esp32_user_exc:
    rsr     a0, exccause
    movi    a1, 4
    bne     a0, a1, .L_esp32_vec_panic
    rsil    a0, 1
    rsr     a2, intenable
    rsr     a3, interrupt
    and     a2, a2, a3
    extui   a4, a2, 6, 1
    beqz    a4, .L_esp32_vec_done
    call0   qemu_esp32_smp_on_timer_irq
.L_esp32_vec_done:
    rfi     1

.L_esp32_vec_panic:
    waiti   0
    j       .L_esp32_vec_panic

    .global qemu_esp32_smp_install_vectors
    .type qemu_esp32_smp_install_vectors, @function
qemu_esp32_smp_install_vectors:
    movi    a2, _qemu_esp32_vector_base
    wsr     a2, vecbase
    isync
    ret
