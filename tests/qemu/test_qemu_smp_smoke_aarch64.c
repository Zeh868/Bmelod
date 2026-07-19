/**
 * @file test_qemu_smp_smoke_aarch64.c
 * @brief QEMU AArch64 virt SMP 冒烟：CPU ID、硬件定时器 tick、串口 TAP 输出
 *
 * 与 RISC-V 版（test_qemu_smp_smoke.c）的区别：UART 为 PL011 @0x09000000，
 * 退出走 PSCI SYSTEM_OFF（hvc #0，QEMU TCG 内建 PSCI 模拟；失败则靠
 * run_qemu_tap.cmake 的 TIMEOUT 兜底）。定时器 tick 递增同时覆盖
 * aarch64_irq_spx 的 x0-x18/q0-q31 现场保存、CPACR_EL1.FPEN 使能、
 * per-CPU tick 计数与 GIC PPI banked 使能。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-16
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-16       1.0            zeh            新增（克隆 rv64 冒烟，换 PL011/PSCI）
 *
 */
#include "hal/bm_hal_cpu.h"
#include "hal/bm_hal_timer.h"

#include <stddef.h>
#include <stdint.h>

/** QEMU aarch64 virt PL011 UART 基址。 */
#define SMOKE_UART_BASE      0x09000000UL
/** PL011 数据寄存器。 */
#define SMOKE_UART_DR        (*(volatile uint32_t *)(SMOKE_UART_BASE + 0x00u))
/** PL011 标志寄存器。 */
#define SMOKE_UART_FR        (*(volatile uint32_t *)(SMOKE_UART_BASE + 0x18u))
/** 发送 FIFO 满。 */
#define SMOKE_UART_FR_TXFF   (1u << 5)

/** PSCI 0.2 SYSTEM_OFF function ID（HVC conduit）。 */
#define SMOKE_PSCI_SYSTEM_OFF  0x84000008ULL

/**
 * @brief 通过 PL011 发送单字节
 *
 * hard-RT 配置按契约禁止阻塞式 UART HAL，因此 smoke 使用平台专用 MMIO
 * 输出测试结果；定时器与 CPU ID 仍通过正式 HAL 验证。
 *
 * @param c 待发送字节
 */
static void smoke_uart_putc(uint8_t c) {
    while ((SMOKE_UART_FR & SMOKE_UART_FR_TXFF) != 0u) {
    }
    SMOKE_UART_DR = (uint32_t)c;
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

/**
 * @brief 请求 PSCI 关机（QEMU 不支持时返回，由 TIMEOUT 兜底）
 */
static void smoke_psci_system_off(void) {
    register uint64_t x0 __asm("x0") = SMOKE_PSCI_SYSTEM_OFF;

    __asm volatile("hvc #0" : : "r"(x0) : "memory");
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
    smoke_psci_system_off();

    for (;;) {
    }
    return 0;
}
