/**
 * @file test_hybrid_smoke.c
 * @brief QEMU 混合域冒烟测试：HRT 定时槽回调是否触发
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 */

#include "bm_hrt.h"
#include "bm_hal_uart.h"
#include "bm_log.h"

#include <string.h>

static volatile uint32_t g_hits;

static void slot_cb(void *context) {
    (void)context;
    g_hits++;
}

int main(void) {
    bm_hrt_slot_t slots[1];
    const char *line;

    BM_LOGI("qemu_hybrid", "start HRT hybrid smoke test");

    slots[0].period_us = 1000u;
    slots[0].trigger = BM_HRT_TRIGGER_TIMER;
    slots[0].callback = slot_cb;
    slots[0].context = NULL;
    slots[0].name = "tick";

    bm_hal_uart_init(NULL);

    if (bm_hrt_init(slots, 1u) != BM_OK || bm_hrt_start() != BM_OK) {
        BM_LOGE("qemu_hybrid", "HRT init/start failed");
        line = "not ok 1 - bm_hrt_init\n";
        bm_hal_uart_send((const uint8_t *)line, strlen(line));
        while (1) {
        }
    }

    /* 忙等一段时间，等待定时槽触发 */
    for (volatile uint32_t i = 0u; i < 500000u; ++i) {
    }

    line = (g_hits > 0u) ? "ok 1 - hybrid_hrt_smoke\n"
                         : "not ok 1 - hybrid_hrt_smoke\n";
    if (g_hits == 0u) {
        BM_LOGE("qemu_hybrid", "HRT slot did not fire");
    }
    bm_hal_uart_send((const uint8_t *)line, strlen(line));
    while (1) {
    }
}
