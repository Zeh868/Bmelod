#ifndef BM_SIM_NATIVE_INTERNAL_H
#define BM_SIM_NATIVE_INTERNAL_H

#include "bm_config.h"
#include "bm_types.h"

#include <stdint.h>

extern volatile uint8_t g_sim_native_isr_depth[];

#if BM_NATIVE_SIM_CPU_LOCAL_CRITICAL
extern volatile bm_irq_state_t g_sim_native_irq_state[];
extern volatile uint8_t g_sim_native_irq_pending[];
#endif

void bm_sim_native_timer_fire_callback(uint32_t cpu);

#endif /* BM_SIM_NATIVE_INTERNAL_H */
