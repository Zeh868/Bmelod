/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_sim_critical_mp_native.c
 * @brief native_sim 多核 per-CPU 临界区（仅 BM_ENABLE_MP 时编译）
 *
 * 单核 PC 测试使用 `bm_port_arch_host`；本文件供 dual_core_stream 等多核仿真。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            从 singleton 拆分
 *
 */
#include "bm_drv_critical.h"
#include "bm_drv_memory.h"
#include "bm_sim_native_internal.h"
#include "hal/bm_hal_cpu.h"

#include <stdint.h>

volatile bm_irq_state_t g_sim_native_irq_state[BM_CONFIG_CPU_COUNT];
volatile uint8_t g_sim_native_irq_pending[BM_CONFIG_CPU_COUNT];

static uint32_t sim_native_cpu_index(void) {
    uint32_t cpu = bm_hal_cpu_id();

    return (cpu < BM_CONFIG_CPU_COUNT) ? cpu : 0u;
}

static bm_irq_state_t sim_native_critical_enter(void) {
    uint32_t cpu = sim_native_cpu_index();
    bm_irq_state_t previous = g_sim_native_irq_state[cpu];

    g_sim_native_irq_state[cpu] = 1;
    return previous;
}

static void sim_native_critical_exit(bm_irq_state_t state) {
    uint32_t cpu = sim_native_cpu_index();

    g_sim_native_irq_state[cpu] = state;
    if (state == 0u && g_sim_native_irq_pending[cpu] != 0u) {
        g_sim_native_irq_pending[cpu] = 0u;
        bm_sim_native_timer_fire_callback(cpu);
    }
}

static int sim_native_in_isr(void) {
    return g_sim_native_isr_depth[sim_native_cpu_index()] != 0u;
}

const struct bm_critical_driver_api bm_drv_critical_api = {
    sim_native_critical_enter,
    sim_native_critical_exit,
    sim_native_in_isr,
};

static void sim_native_memory_release(void) {
}

static void sim_native_memory_full(void) {
}

const struct bm_memory_driver_api bm_drv_memory_api = {
    sim_native_memory_release,
    sim_native_memory_full,
};
