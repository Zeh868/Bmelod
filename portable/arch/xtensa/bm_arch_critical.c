/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_critical.c
 * @brief Xtensa 临界区实现（PS 中断级保存/恢复）
 *
 * ESP32 LX6/LX7：`rsil` 提升中断屏蔽级，`wsr.ps` + `rsync` 恢复。
 * ISR 检测优先 ESP-IDF `xPortInIsrContext()`，其次 `xthal_get_intlevel()`。
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            Phase 3 正式发布
 * 2026-06-15       1.1            zeh            使用 rsil 原子进入临界区
 *
 */
#include "port/bm_arch_ops.h"

#include <stdint.h>

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#endif

#if defined(__has_include)
#if __has_include(<xtensa/hal.h>)
#include <xtensa/hal.h>
#define BM_XTENSA_HAS_XTHAL 1
#endif
#endif

static inline uint32_t bm_xtensa_raise_intlevel(void) {
    uint32_t ps;
    __asm__ volatile ("rsil %0, 15" : "=a"(ps) :: "memory");
    return ps;
}

static inline void bm_xtensa_write_ps(uint32_t ps) {
    __asm__ volatile ("wsr.ps %0" :: "a"(ps) : "memory");
    __asm__ volatile ("rsync");
}

bm_irq_state_t bm_arch_critical_enter(void) {
    uint32_t ps = bm_xtensa_raise_intlevel();
    return (bm_irq_state_t)ps;
}

void bm_arch_critical_exit(bm_irq_state_t state) {
    bm_xtensa_write_ps((uint32_t)state);
}

int bm_arch_in_isr(void) {
#if defined(ESP_PLATFORM)
    return xPortInIsrContext() ? 1 : 0;
#elif defined(BM_XTENSA_HAS_XTHAL)
    return (xthal_get_intlevel() > 0) ? 1 : 0;
#else
    /*
     * 无 ESP-IDF / xthal 头时的保守桩：始终返回线程上下文。
     * 仅用于缺少 SDK 的语法检查；真机须链接 ESP-IDF 或提供 xthal。
     */
    return 0;
#endif
}
