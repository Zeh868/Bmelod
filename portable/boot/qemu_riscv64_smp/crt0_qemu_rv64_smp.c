/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file crt0_qemu_rv64_smp.c
 * @brief QEMU RISC-V64 virt SMP 启动：.data/.bss 初始化与从核 mailbox
 *
 * 从核由 startup 汇编唤醒后经 qemu_rv64_smp_secondary_start 跳入框架入口。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            正式发布
 *
 */
#include "bm_config.h"

#include <stdint.h>

extern uintptr_t _sidata;
extern uintptr_t _sdata;
extern uintptr_t _edata;
extern uintptr_t _sbss;
extern uintptr_t _ebss;

/** 各 hart 从核唤醒 mailbox（由主核写入入口 PC） */
volatile uintptr_t g_secondary_mailbox[BM_CONFIG_CPU_COUNT];

/** 从核已退出标志（join 路径自旋等待） */
volatile uint32_t g_secondary_done[BM_CONFIG_CPU_COUNT];

/** 主核 SystemInit 完成标志（从核在 BSS 有效前不得读 mailbox） */
volatile uint32_t g_cpu0_system_init_done;

void SystemInit(void) {
    uintptr_t *src = (uintptr_t *)&_sidata;
    uintptr_t *dst = (uintptr_t *)&_sdata;

    while (dst < (uintptr_t *)&_edata) {
        *dst++ = *src++;
    }

    dst = (uintptr_t *)&_sbss;
    while (dst < (uintptr_t *)&_ebss) {
        *dst++ = 0;
    }
    g_cpu0_system_init_done = 1u;
    __asm volatile("fence rw, rw" ::: "memory");
}

/**
 * @brief 从核汇编唤醒后的 C 跳板：清 mailbox、调用入口、标记完成
 *
 * @param entry 主核写入的从核入口函数地址
 */
void qemu_rv64_smp_secondary_start(uintptr_t entry) {
    uint32_t cpu;
    uintptr_t fn = entry;

    __asm volatile ("csrr %0, mhartid" : "=r"(cpu));

    if (cpu < BM_CONFIG_CPU_COUNT) {
        g_secondary_mailbox[cpu] = 0u;
    }
    if (fn) {
        ((void (*)(void))fn)();
    }
    if (cpu < BM_CONFIG_CPU_COUNT) {
        g_secondary_done[cpu] = 1u;
    }
}
