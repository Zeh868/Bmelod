/**
 * @file test_qemu_uptime_smoke.c
 * @brief QEMU 冒烟测试：单调时钟 bm_uptime_ns() 读数随时间递增
 *
 * 验证点：
 * 1. 初始化 TIMER1（1000 Hz）后经过若干 tick，bm_uptime_ns() > 0；
 * 2. 连续两次调用 bm_uptime_ns() 满足单调不减；
 * 3. bm_uptime_us() 与 ns/1000 量级一致。
 *
 * 使用 WFI 让 QEMU 调度 TIMER1 中断，确保 tick 计数器推进。
 * 输出格式遵循 TAP（ok/not ok N - <name>）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-26       1.0            zeh            正式发布（路线图 #9 时间基统一 1a）
 *
 */
#include "bm_hal_uart.h"
#include "bm_hal_timer.h"
#include "bm/common/bm_uptime.h"
#include "bm_log.h"
#include <string.h>
#include <stdint.h>

/** @brief 等待至少 n 次 WFI（每次唤醒对应一个 TIMER1 中断）*/
static void wait_ticks(uint32_t n) {
    uint32_t i;

    for (i = 0u; i < n; i++) {
        __asm volatile("wfi");
    }
}

/** @brief 向串口发送字符串 */
static void uart_puts(const char *s) {
    bm_hal_uart_send((const uint8_t *)s, strlen(s));
}

/** @brief 发送 TAP 行并等待串口刷新 */
static void tap_result(int tap_num, int pass, const char *name) {
    char buf[64];
    const char *status = pass ? "ok" : "not ok";
    /* 简单的字符串拼接，避免引入 snprintf */
    uart_puts(status);
    uart_puts(" ");
    buf[0] = (char)('0' + (tap_num % 10));
    buf[1] = '\0';
    uart_puts(buf);
    uart_puts(" - ");
    uart_puts(name);
    uart_puts("\n");
}

int main(void) {
    uint64_t t0, t1, us;
    int pass_nonzero, pass_mono, pass_us;

    bm_hal_uart_init(NULL);
    BM_LOGI("uptime_smoke", "start uptime smoke test");

    /* 启动 TIMER1（1 kHz），使 bm_hal_uptime_ns_raw() 的 tick 开始递增 */
    (void)bm_hal_timer_init(1000u);

    /* 等待至少 3 次 timer 中断（WFI 会在中断返回后继续）*/
    wait_ticks(3u);

    t0 = bm_uptime_ns();

    /* 再等一次 tick，确保 t1 > t0 */
    wait_ticks(1u);

    t1 = bm_uptime_ns();
    us = bm_uptime_us();

    /* 验证 1：t0 > 0（计时器已推进，非全 0 桩）*/
    pass_nonzero = (t0 > 0u);

    /* 验证 2：t1 >= t0（单调不减）*/
    pass_mono = (t1 >= t0);

    /* 验证 3：us 与 t1/1000 量级一致（us 在 t1 之后读，允许 <=1 s 的差）*/
    pass_us = (us >= (t1 / 1000u)) && ((us - (t1 / 1000u)) < 1000000u);

    tap_result(1, pass_nonzero, "qemu_uptime_nonzero");
    tap_result(2, pass_mono,    "qemu_uptime_monotonic");
    tap_result(3, pass_us,      "qemu_uptime_us_consistent");

    while (1) {}
}
