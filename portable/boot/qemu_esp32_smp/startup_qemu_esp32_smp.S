/* SPDX-License-Identifier: GPL-3.0-or-later */
    .section .iram1.text, "ax"
    .global _start
    .type _start, @function
    .align 4
_start:
    /* 复位置 PS=0x1F（EXCM=1 / INTLEVEL=0xF / WOE=0）：windowed ABI 的溢出/
       下溢异常与全部中断均不可用，直接跑 C 必腐败。两核入口先落线程态
       PS = PS_WOE(0x40000)|PS_UM(0x20)|INTLEVEL(0)，再设栈。 */
    movi    a0, 0x40020
    wsr     a0, ps
    rsync

    /* PRO_CPU：设置栈指针 */
    movi    a1, _estack_cpu0

    /* 读 CPU ID（PRID bit13：0=PRO，1=APP） */
    rsr.prid a0
    extui   a0, a0, 13, 1
    bnez    a0, .app_cpu_park

    /* PRO_CPU 路径：安装向量表并启动 */
    call0   qemu_esp32_smp_install_vectors
    call0   SystemInit
    call0   main
1:
    waiti   0
    j       1b

.app_cpu_park:
    /* APP_CPU 正常由 DPORT 释放后直接跳入 entry_pc；此处为安全兜底 */
    waiti   0
    j       .app_cpu_park

    .section .iram1.text, "ax"
    .global bm_esp32_secondary_startup
    .type bm_esp32_secondary_startup, @function
    .align 4
bm_esp32_secondary_startup:
    /* DPORT 释放 APP_CPU 后跳入此处；APP 核同样先落线程态 PS（复位 PS 同 PRO） */
    movi    a0, 0x40020
    wsr     a0, ps
    rsync
    call0   qemu_esp32_smp_install_vectors
    movi    a1, _estack_cpu1
    call0   bm_esp32_secondary_entry_c
2:
    waiti   0
    j       2b

    .extern qemu_esp32_smp_install_vectors
