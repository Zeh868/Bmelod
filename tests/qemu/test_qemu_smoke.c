/**
 * @file test_qemu_smoke.c
 * @brief QEMU 冒烟测试：事件 publish_copy 与订阅回调
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 */

#include "bm_core.h"
#include "bm_hal_uart.h"
#include "bm_log.h"
#include <string.h>

static int g_evt_count = 0;

void cb(const bm_event_t *ev, void *ud) {
    (void)ev; (void)ud;
    g_evt_count++;
}

int main(void) {
    BM_LOGI("qemu_smoke", "start event publish smoke test");
    bm_hal_uart_init(NULL);

    bm_event_register_type(1, "TEST");
    bm_event_subscriber_id_t id;
    bm_event_subscribe(1, cb, NULL, &id);

    uint8_t data = 0xAB;
    bm_event_publish_copy(1, 0, &data, sizeof(data));
    bm_event_process(8);

    const char *pass = (g_evt_count == 1) ? "ok 1 - event_publish_copy\n"
                                            : "not ok 1 - event_publish_copy\n";
    if (g_evt_count != 1) {
        BM_LOGE("qemu_smoke", "unexpected callback count: %d", g_evt_count);
    }
    bm_hal_uart_send((const uint8_t *)pass, strlen(pass));

    while (1) {}
}
