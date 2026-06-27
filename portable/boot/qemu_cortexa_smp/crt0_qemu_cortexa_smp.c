/**
 * @file crt0_qemu_cortexa_smp.c
 * @brief QEMU ARMv7-A virt SMP 启动：复制 .data 并清零 .bss
 *
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
#include <stdint.h>

extern uintptr_t _sidata;
extern uintptr_t _sdata;
extern uintptr_t _edata;
extern uintptr_t _sbss;
extern uintptr_t _ebss;

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
