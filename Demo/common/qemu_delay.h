/**
 * @file qemu_delay.h
 * @brief QEMU 示例用忙等待延时辅助
 *
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
#ifndef QEMU_DELAY_H
#define QEMU_DELAY_H

#include <stdint.h>

/** 让 QEMU 仿真时间推进，使 TIMER1 中断有机会触发 */
static inline void bm_example_qemu_spin(void) {
    for (volatile uint32_t i = 0u; i < 8000u; ++i) {
    }
}

static inline void bm_example_qemu_warmup(void) {
    for (volatile uint32_t i = 0u; i < 400000u; ++i) {
    }
}

#endif /* QEMU_DELAY_H */
