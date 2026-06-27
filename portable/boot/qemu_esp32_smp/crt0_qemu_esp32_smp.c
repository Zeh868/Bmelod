/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file crt0_qemu_esp32_smp.c
 * @brief QEMU ESP32 Xtensa SMP 启动：.data/.bss 初始化与 APP_CPU 入口桩
 *
 * APP_CPU 由 DPORT 释放后直接跳入 bm_esp32_secondary_startup，再经本文件 C 包装调用框架入口。
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

/** 从核已退出标志（join 路径自旋等待） */
volatile uint32_t g_secondary_done[BM_CONFIG_CPU_COUNT];

/** 主核写入的从核框架入口（由 CPU HAL 设置） */
extern volatile uintptr_t s_esp32_secondary_entry;

/**
 * @brief 读 PRID 提取 PRO(0) / APP(1) 核号
 */
static uint32_t esp32_boot_core_id(void) {
    uint32_t id;

    __asm__ volatile("rsr.prid %0" : "=a"(id));
    return (id >> 13) & 1u;
}

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
}

/**
 * @brief APP_CPU 汇编入口后的 C 包装：调用框架入口并标记完成
 */
void bm_esp32_secondary_entry_c(void) {
    uintptr_t entry = s_esp32_secondary_entry;
    uint32_t cpu = esp32_boot_core_id();

    if (entry) {
        ((void (*)(void))entry)();
    }
    if (cpu < BM_CONFIG_CPU_COUNT) {
        g_secondary_done[cpu] = 1u;
    }
}
