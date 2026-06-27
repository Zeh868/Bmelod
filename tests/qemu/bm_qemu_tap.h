/**
 * @file bm_qemu_tap.h
 * @brief QEMU ARMv7-A virt 裸机测试共享基建：PL011 UART + TAP 输出 + PSCI 退出
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-25
 */
#ifndef BM_QEMU_TAP_H
#define BM_QEMU_TAP_H

#include <stddef.h>
#include <stdint.h>

/* PL011 UART @ QEMU ARM virt */
#define BM_QEMU_UART_BASE    0x09000000UL
#define BM_QEMU_UART_DR      (*(volatile uint32_t *)(BM_QEMU_UART_BASE + 0x000))
#define BM_QEMU_UART_FR      (*(volatile uint32_t *)(BM_QEMU_UART_BASE + 0x018))
#define BM_QEMU_UART_FR_TXFF (1u << 5)

static inline void bm_qemu_uart_putc(uint8_t c) {
    while ((BM_QEMU_UART_FR & BM_QEMU_UART_FR_TXFF) != 0u) { }
    BM_QEMU_UART_DR = (uint32_t)c;
}

static inline void bm_qemu_uart_puts(const char *s) {
    size_t n = 0u;
    if (!s) { return; }
    while (s[n] != '\0') { bm_qemu_uart_putc((uint8_t)s[n]); n++; }
}

static inline void bm_qemu_uart_put_u32(uint32_t v) {
    char buf[11];
    int i = 0;
    int j;
    if (v == 0u) { bm_qemu_uart_puts("0"); return; }
    while (v > 0u && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10u)); v /= 10u;
    }
    for (j = i - 1; j >= 0; j--) { bm_qemu_uart_putc((uint8_t)buf[j]); }
}

/** PSCI SYSTEM_OFF（32-bit HVC）退出 QEMU */
static inline void bm_qemu_exit(void) {
    register uint32_t r0 __asm("r0") = 0x84000008u;
    __asm volatile("hvc #0" : "+r"(r0) :: "memory");
    for (;;) { __asm volatile("wfi"); }
}

typedef struct {
    const char *name;
    int         pass;
    uint32_t    info;
    int         skip;   /**< 非 0 = 标记为 TAP SKIP（输出 "ok N - name # SKIP"），不计失败 */
} bm_qemu_result_t;

typedef struct {
    bm_qemu_result_t *items;
    uint32_t          cap;
    uint32_t          cnt;
    const char       *prefix;   /**< 如 "cpu0" / "cpu1" */
} bm_qemu_results_t;

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

/**
 * @brief 记录一条 TAP SKIP 条目（pass=1、skip=1，输出为 "ok N - prefix:name # SKIP"）
 *
 * 用于单核 -smp 1 场景：需要第二核才能运行的测试项记 SKIP，计为通过、不触发 FAILED。
 *
 * @param r    结果集指针
 * @param name 测试名称
 */
static inline void bm_qemu_record_skip(bm_qemu_results_t *r, const char *name) {
    if (r && r->cnt < r->cap) {
        r->items[r->cnt].name = name;
        r->items[r->cnt].pass = 1;
        r->items[r->cnt].info = 0u;
        r->items[r->cnt].skip = 1;
        r->cnt++;
    }
}

/** 输出多组结果为 TAP，再调用 bm_qemu_exit() */
static inline void bm_qemu_print_tap(const bm_qemu_results_t *sets,
                                     uint32_t set_count, const char *suite) {
    uint32_t total = 0u;
    uint32_t s;
    uint32_t i;
    uint32_t idx = 0u;
    int all_pass = 1;

    for (s = 0u; s < set_count; s++) { total += sets[s].cnt; }
    bm_qemu_uart_puts("TAP version 13\n1..");
    bm_qemu_uart_put_u32(total);
    bm_qemu_uart_puts("\n");

    for (s = 0u; s < set_count; s++) {
        for (i = 0u; i < sets[s].cnt; i++) {
            idx++;
            if (sets[s].items[i].pass) {
                bm_qemu_uart_puts("ok ");
            } else {
                bm_qemu_uart_puts("not ok ");
                all_pass = 0;
            }
            bm_qemu_uart_put_u32(idx);
            bm_qemu_uart_puts(" - ");
            bm_qemu_uart_puts(sets[s].prefix);
            bm_qemu_uart_puts(":");
            bm_qemu_uart_puts(sets[s].items[i].name);
            if (sets[s].items[i].skip) {
                bm_qemu_uart_puts(" # SKIP");
            } else if (!sets[s].items[i].pass) {
                bm_qemu_uart_puts(" # info=");
                bm_qemu_uart_put_u32(sets[s].items[i].info);
            }
            bm_qemu_uart_puts("\n");
        }
    }
    bm_qemu_uart_puts("# ");
    bm_qemu_uart_puts(suite);
    bm_qemu_uart_puts(all_pass ? ": PASSED\n" : ": FAILED\n");
    bm_qemu_exit();
}

#endif /* BM_QEMU_TAP_H */
