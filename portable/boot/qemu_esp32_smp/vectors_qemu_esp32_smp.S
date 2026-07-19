/* SPDX-License-Identifier: GPL-3.0-or-later */
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

/* WindowUnderflow8 @ 0xC0（ISA 规范序列，对齐 IDF xtensa_vectors.S） */
    .org _qemu_esp32_vector_base + 0xC0
    .global _WindowUnderflow8
_WindowUnderflow8:
    l32e    a0, a9, -16     /* 经 call[i+1] 栈帧恢复 call[i] 的 a0-a2 */
    l32e    a1, a9, -12
    l32e    a2, a9, -8
    l32e    a7, a1, -12     /* a7 <- call[i-1] 的 sp，用于定位 call[i] 帧尾 */
    l32e    a3, a9, -4
    l32e    a4, a7, -32     /* 经 call[i] 帧尾恢复 a4-a7 */
    l32e    a5, a7, -28
    l32e    a6, a7, -24
    l32e    a7, a7, -20
    rfwu

/* WindowOverflow12 @ 0x100（ISA 规范序列，对齐 IDF xtensa_vectors.S） */
    .org _qemu_esp32_vector_base + 0x100
    .global _WindowOverflow12
_WindowOverflow12:
    s32e    a0,  a13, -16   /* a0-a3 存入 call[j+1] 栈帧 */
    l32e    a0,  a1,  -12   /* a0 <- call[j-1] 的 sp，用于定位 call[j] 帧尾 */
    s32e    a1,  a13, -12
    s32e    a2,  a13, -8
    s32e    a3,  a13, -4
    s32e    a4,  a0,  -48   /* a4-a11 存入 call[j] 帧尾 */
    s32e    a5,  a0,  -44
    s32e    a6,  a0,  -40
    s32e    a7,  a0,  -36
    s32e    a8,  a0,  -32
    s32e    a9,  a0,  -28
    s32e    a10, a0,  -24
    s32e    a11, a0,  -20
    rfwo

/* WindowUnderflow12 @ 0x140（ISA 规范序列，对齐 IDF xtensa_vectors.S） */
    .org _qemu_esp32_vector_base + 0x140
    .global _WindowUnderflow12
_WindowUnderflow12:
    l32e    a0,  a13, -16   /* 经 call[i+1] 栈帧恢复 call[i] 的 a0-a2 */
    l32e    a1,  a13, -12
    l32e    a2,  a13, -8
    l32e    a11, a1,  -12   /* a11 <- call[i-1] 的 sp，用于定位 call[i] 帧尾 */
    l32e    a3,  a13, -4
    l32e    a4,  a11, -48   /* 经 call[i] 帧尾恢复 a4-a11 */
    l32e    a5,  a11, -44
    l32e    a6,  a11, -40
    l32e    a7,  a11, -36
    l32e    a8,  a11, -32
    l32e    a9,  a11, -28
    l32e    a10, a11, -24
    l32e    a11, a11, -20
    rfwu

/* Level2..6 / NMI-Debug / Kernel / Double：未使用，显式自旋兜底。
   XEA2 固定偏移，VECBASE 表共 0x400 字节（0x00..0x3FF）；
   原 0x400/0x410/0x470 占位在表外且无任何硬件入口，删除。 */
    .org _qemu_esp32_vector_base + 0x180
1:  waiti   0
    j       1b

    .org _qemu_esp32_vector_base + 0x1C0
2:  waiti   0
    j       2b

    .org _qemu_esp32_vector_base + 0x200
3:  waiti   0
    j       3b

    .org _qemu_esp32_vector_base + 0x240
4:  waiti   0
    j       4b

    .org _qemu_esp32_vector_base + 0x280
5:  waiti   0
    j       5b

    .org _qemu_esp32_vector_base + 0x2C0
6:  waiti   0
    j       6b

    .org _qemu_esp32_vector_base + 0x300
7:  waiti   0
    j       7b

/* UserException @ 0x340：XEA2 上 level-1 中断（EXCCAUSE=4）的唯一入口。
   其余 UserException 原因（syscall/alloca/非法指令等）进 panic 自旋。 */
    .org _qemu_esp32_vector_base + 0x340
    .global _UserExceptionVector
_UserExceptionVector:
    wsr     a0, excsave1
    call0   _qemu_esp32_user_exc

    .org _qemu_esp32_vector_base + 0x3C0
8:  waiti   0
    j       8b

    .section .iram1.text, "ax"
    .align 4
    .type _qemu_esp32_user_exc, @function
_qemu_esp32_user_exc:
    /* a0 原值已由向量存入 excsave1；call0 使 a0=返回地址（死值），全程可作 scratch */
    rsr     a0, exccause
    addi    a0, a0, -4          /* EXCCAUSE = Level1Interrupt(4)？ */
    bnez    a0, .L_esp32_vec_panic

    /* 保存现场：a2-a15（windowed C 经 entry/retw 可能占用整个当前窗口），
       外加 a0、PS 与 EPC1——XEA2 的 level-1 中断走 UserException 路径，硬件只写
       EPC1、不存在 EPS1，原 PS 只能由软件自存自恢（出口 rfe 也不还原 PS，
       只清 EXCM、跳 EPC1）；EPC1 防 C 侧异常覆写。 */
    addi    a1, a1, -80
    s32i    a2, a1, 0
    s32i    a3, a1, 4
    s32i    a4, a1, 8
    s32i    a5, a1, 12
    s32i    a6, a1, 16
    s32i    a7, a1, 20
    s32i    a8, a1, 24
    s32i    a9, a1, 28
    s32i    a10, a1, 32
    s32i    a11, a1, 36
    s32i    a12, a1, 40
    s32i    a13, a1, 44
    s32i    a14, a1, 48
    s32i    a15, a1, 52
    rsr     a2, excsave1        /* 被中断代码的 a0；excsave1 仅作入口摆渡 */
    s32i    a2, a1, 56
    rsr     a2, ps
    s32i    a2, a1, 60          /* 被打断现场 PS（硬件已置 EXCM=1） */
    rsr     a2, epc1
    s32i    a2, a1, 64

    rsil    a0, 1
    /* 清 PS.EXCM、置 PS.WOE：windowed-ABI C 的 entry/retw 依赖窗口溢出/下溢
       异常，EXCM=1 会将其屏蔽（静默窗口腐败）；且 windowed 被调方经 retw
       取 caller a4 作返回地址，必须用 callx4 蹦床而非 call0。 */
    rsr     a0, ps
    movi    a2, -17             /* ~PS_EXCM(0x10) */
    and     a0, a0, a2
    movi    a2, 4
    slli    a2, a2, 16          /* PS_WOE(0x40000) */
    or      a0, a0, a2
    wsr     a0, ps
    rsync
    rsr     a2, intenable
    rsr     a3, interrupt
    and     a2, a2, a3
    extui   a4, a2, 6, 1
    beqz    a4, .L_esp32_vec_restore
    movi    a0, qemu_esp32_smp_on_timer_irq
    callx4  a0

.L_esp32_vec_restore:
    /* 出口全程禁止 level-1 重入：趁 INTLEVEL=1（L1 被屏蔽）先写回 EPC1、
       组装最终 PS（入口 PS 硬件已置 EXCM=1，此处防御性再置位）；恢复全部
       通用寄存器后写入最终 PS——EXCM=1 屏蔽 ≤EXCM_LEVEL 的全部中断，堵住
       "PS 恢复 INTLEVEL=0 到 rfe 之间"的嵌套窗口（否则嵌套 L1 会覆写
       EPC1，rfe 将跳回出口自身死循环）；最后 rfe 原子清 EXCM、跳 EPC1，
       被打断代码看到的恰是其原 PS。 */
    l32i    a2, a1, 64
    wsr     a2, epc1
    l32i    a2, a1, 60
    movi    a3, 16              /* PS_EXCM(0x10) */
    or      a2, a2, a3          /* a2 = 最终 PS，保留到尾部 wsr */
    l32i    a3, a1, 4
    l32i    a4, a1, 8
    l32i    a5, a1, 12
    l32i    a6, a1, 16
    l32i    a7, a1, 20
    l32i    a8, a1, 24
    l32i    a9, a1, 28
    l32i    a10, a1, 32
    l32i    a11, a1, 36
    l32i    a12, a1, 40
    l32i    a13, a1, 44
    l32i    a14, a1, 48
    l32i    a15, a1, 52
    l32i    a0, a1, 56          /* 被中断代码的 a0 */
    wsr     a2, ps
    rsync
    l32i    a2, a1, 0           /* EXCM=1 下无嵌套，帧内容稳定 */
    addi    a1, a1, 80
    rfe

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
