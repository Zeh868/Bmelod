/**
 * @file example_support.c
 * @brief 示例公共辅助函数实现
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 *
 */
#include "example_support.h"

#include "hal/bm_hal_uart.h"
#include "bm_log.h"

#include <stddef.h>
#include <string.h>

#define TAG "example"

#if defined(BM_EXAMPLE_QEMU_SMP)
/**
 * @brief QEMU 裸机演示：绕过 hard RT 剖面的 UART 门控直写 ns16550a
 */
static void example_qemu_uart_send(const uint8_t *data, size_t len) {
    volatile uint8_t *thr = (volatile uint8_t *)0x10000000UL;
    volatile uint8_t *lsr = (volatile uint8_t *)0x10000005UL;
    size_t i;

    if (!data || len == 0u) {
        return;
    }
    for (i = 0u; i < len; i++) {
        while ((*lsr & 0x20u) == 0u) {
        }
        *thr = data[i];
    }
}
#endif

void example_print(const char *text) {
    if (text) {
#if defined(BM_EXAMPLE_QEMU_SMP)
        example_qemu_uart_send((const uint8_t *)text, strlen(text));
#else
        bm_hal_uart_send((const uint8_t *)text, strlen(text));
#endif
    }
}

void example_print_u32(uint32_t value) {
    char reversed[10];
    char output[11];
    size_t count = 0;

    /* 将数字倒序存入 reversed */
    do {
        reversed[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value > 0U);

    /* 反转得到正序十进制字符串 */
    for (size_t i = 0; i < count; i++) {
        output[i] = reversed[count - 1U - i];
    }
    output[count] = '\0';
    BM_LOGD(TAG, "print_u32: %s", output);
#if defined(BM_EXAMPLE_QEMU_SMP)
    example_qemu_uart_send((const uint8_t *)output, count);
#else
    bm_hal_uart_send((const uint8_t *)output, count);
#endif
}

void example_delay_cycles(uint32_t cycles) {
    for (volatile uint32_t i = 0; i < cycles; i++) {
    }
}
