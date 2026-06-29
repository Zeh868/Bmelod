/**
 * @file test_qemu_smp_smoke.c
 * @brief QEMU SMP 冒烟：CPU ID、硬件定时器 tick、串口 TAP 输出
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
#include "hal/bm_hal_cpu.h"
#include "hal/bm_hal_timer.h"

#include <stddef.h>
#include <stdint.h>

/** QEMU virt ns16550a UART 基址。 */
#define SMOKE_UART_BASE      0x10000000UL
/** UART 发送保持寄存器。 */
#define SMOKE_UART_THR       (*(volatile uint8_t *)(SMOKE_UART_BASE + 0u))
/** UART 线路状态寄存器。 */
#define SMOKE_UART_LSR       (*(volatile uint8_t *)(SMOKE_UART_BASE + 5u))
/** 发送保持寄存器为空。 */
#define SMOKE_UART_LSR_THRE  (1u << 5)
/** QEMU virt 测试完成寄存器。 */
#define SMOKE_QEMU_TEST      (*(volatile uint32_t *)0x100000UL)
/** QEMU virt 测试成功值。 */
#define SMOKE_QEMU_PASS      0x5555u

/**
 * @brief 通过 QEMU 诊断串口发送单字节
 *
 * hard-RT 配置按契约禁止阻塞式 UART HAL，因此 smoke 使用平台专用 MMIO
 * 输出测试结果；定时器与 CPU ID 仍通过正式 HAL 验证。
 *
 * @param c 待发送字节
 */
static void smoke_uart_putc(uint8_t c) {
    while ((SMOKE_UART_LSR & SMOKE_UART_LSR_THRE) == 0u) {
    }
    SMOKE_UART_THR = c;
}

/**
 * @brief 经 UART 发送以 NUL 结尾的字符串
 */
static void smoke_uart_puts(const char *s) {
    size_t n = 0u;

    if (!s) {
        return;
    }
    while (s[n] != '\0') {
        smoke_uart_putc((uint8_t)s[n]);
        n++;
    }
}

/**
 * @brief 无 libc 的十进制无符号输出
 */
static void smoke_uart_put_u32(uint32_t v) {
    char buf[11];
    int i = 0;
    int j;

    if (v == 0u) {
        smoke_uart_puts("0");
        return;
    }
    while (v > 0u && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    for (j = i - 1; j >= 0; j--) {
        smoke_uart_putc((uint8_t)buf[j]);
    }
}

int main(void) {
    uint32_t cpu = bm_hal_cpu_id();
    uint32_t start;
    uint32_t now;

    (void)bm_hal_timer_init(1000u);

    smoke_uart_puts("smp_smoke: cpu=");
    smoke_uart_put_u32(cpu);
    smoke_uart_puts("\n");

    start = bm_hal_timer_get_ticks();
    do {
        now = bm_hal_timer_get_ticks();
    } while ((now - start) < 5u);

    smoke_uart_puts("ok 1 - smp_smoke cpu=");
    smoke_uart_put_u32(cpu);
    smoke_uart_puts("\n");
    SMOKE_QEMU_TEST = SMOKE_QEMU_PASS;

    for (;;) {
    }
    return 0;
}
