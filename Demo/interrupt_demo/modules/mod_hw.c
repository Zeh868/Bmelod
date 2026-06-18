#include "hal/bm_hal_timer.h"
#include "bm_log.h"
#include "bm_module.h"
#include "interrupt_timer.h"

#define TAG "irq_demo"
#define SYSTICK_FREQ_HZ 10U

extern void irq_publish_tick_from_isr(void);
extern void irq_publish_sample_from_isr(void);

static int hw_start(void) {
    bm_hal_timer_set_callback(irq_publish_tick_from_isr);
    if (bm_hal_timer_init(SYSTICK_FREQ_HZ) != 0) {
        BM_LOGE(TAG, "systick init failed");
        return BM_ERR_NOT_INIT;
    }
    interrupt_timer_init(irq_publish_sample_from_isr);
    BM_LOGI(TAG, "hw started, systick=%u Hz", (unsigned)SYSTICK_FREQ_HZ);
    return BM_OK;
}

BM_MODULE_DEFINE(hw, 4, NULL, hw_start, NULL, NULL);
