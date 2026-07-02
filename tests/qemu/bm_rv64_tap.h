/**
 * @file bm_rv64_tap.h
 * @brief QEMU RISC-V64 virt 裸机测试基建：ns16550a UART + TAP + sifive_test 退出
 *
 * 对标 bm_qemu_tap.h（ARM 版），提供相同 bm_qemu_result_t / bm_qemu_record /
 * bm_qemu_print_tap 接口，但使用 RV64 virt 平台外设（地址与 test_qemu_smp_smoke.c 一致）：
 *   - ns16550a UART @ 0x10000000（THR offset 0，LSR offset 5，THRE=1<<5）
 *   - sifive_test 退出设备 @ 0x100000（0x5555=pass，0x3333=fail）
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-07-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-01       0.1            zeh            Task 7：RV64 TAP 基建
 *
 */
#ifndef BM_RV64_TAP_H
#define BM_RV64_TAP_H

#include <stddef.h>
#include <stdint.h>

/* ns16550a UART @ QEMU RISC-V64 virt */
#define BM_RV64_UART_BASE   0x10000000UL
#define BM_RV64_UART_THR    (*(volatile uint8_t *)(BM_RV64_UART_BASE + 0u))
#define BM_RV64_UART_LSR    (*(volatile uint8_t *)(BM_RV64_UART_BASE + 5u))
#define BM_RV64_UART_THRE   (1u << 5)

/* sifive_test 设备（QEMU virt RISC-V 标准退出设备） */
#define BM_RV64_TEST_ADDR   (*(volatile uint32_t *)0x100000UL)
#define BM_RV64_TEST_PASS   0x5555u
#define BM_RV64_TEST_FAIL   0x3333u

/** @brief 经 UART 发送一个字节（忙等 THRE） */
static inline void bm_rv64_uart_putc(uint8_t c) {
    while ((BM_RV64_UART_LSR & BM_RV64_UART_THRE) == 0u) {}
    BM_RV64_UART_THR = c;
}

/** @brief 经 UART 发送以 NUL 结尾字符串 */
static inline void bm_rv64_uart_puts(const char *s) {
    size_t n = 0u;
    if (!s) { return; }
    while (s[n] != '\0') { bm_rv64_uart_putc((uint8_t)s[n]); n++; }
}

/** @brief 经 UART 十进制打印 uint32 */
static inline void bm_rv64_uart_put_u32(uint32_t v) {
    char buf[11];
    int  i = 0;
    int  j;
    if (v == 0u) { bm_rv64_uart_puts("0"); return; }
    while (v > 0u && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10u)); v /= 10u;
    }
    for (j = i - 1; j >= 0; j--) { bm_rv64_uart_putc((uint8_t)buf[j]); }
}

/** @brief 经 sifive_test 退出 QEMU（pass!=0 写 0x5555，否则 0x3333） */
static inline void bm_rv64_exit(int pass) {
    BM_RV64_TEST_ADDR = (uint32_t)(pass ? BM_RV64_TEST_PASS : BM_RV64_TEST_FAIL);
    for (;;) { __asm__ volatile("wfi"); }
}

/* ---- 与 bm_qemu_tap.h 同等接口（便于两线代码复用） ---- */

typedef struct {
    const char *name;
    int         pass;
    uint32_t    info;
    int         skip;
} bm_qemu_result_t;

typedef struct {
    bm_qemu_result_t *items;
    uint32_t          cap;
    uint32_t          cnt;
    const char       *prefix;
} bm_qemu_results_t;

/** @brief 记录一条测试结果 */
static inline void bm_qemu_record(bm_qemu_results_t *r, const char *name,
                                  int pass, uint32_t info) {
    if (r && r->cnt < r->cap) {
        r->items[r->cnt].name = name;
        r->items[r->cnt].pass = pass;
        r->items[r->cnt].info = info;
        r->items[r->cnt].skip = 0;
        r->cnt++;
    }
}

/** @brief 输出多组结果为 TAP，再经 sifive_test 退出 */
static inline void bm_qemu_print_tap(const bm_qemu_results_t *sets,
                                     uint32_t set_count, const char *suite) {
    uint32_t total = 0u;
    uint32_t s;
    uint32_t i;
    uint32_t idx = 0u;
    int      all_pass = 1;

    for (s = 0u; s < set_count; s++) { total += sets[s].cnt; }
    bm_rv64_uart_puts("TAP version 13\n1..");
    bm_rv64_uart_put_u32(total);
    bm_rv64_uart_puts("\n");

    for (s = 0u; s < set_count; s++) {
        for (i = 0u; i < sets[s].cnt; i++) {
            idx++;
            if (sets[s].items[i].pass) {
                bm_rv64_uart_puts("ok ");
            } else {
                bm_rv64_uart_puts("not ok ");
                all_pass = 0;
            }
            bm_rv64_uart_put_u32(idx);
            bm_rv64_uart_puts(" - ");
            bm_rv64_uart_puts(sets[s].prefix);
            bm_rv64_uart_puts(":");
            bm_rv64_uart_puts(sets[s].items[i].name);
            if (!sets[s].items[i].pass) {
                bm_rv64_uart_puts(" # info=");
                bm_rv64_uart_put_u32(sets[s].items[i].info);
            }
            bm_rv64_uart_puts("\n");
        }
    }
    bm_rv64_uart_puts("# ");
    bm_rv64_uart_puts(suite);
    bm_rv64_uart_puts(all_pass ? ": PASSED\n" : ": FAILED\n");
    bm_rv64_exit(all_pass);
}

#endif /* BM_RV64_TAP_H */
