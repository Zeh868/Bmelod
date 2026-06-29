/**
 * @file main.c
 * @brief 双核电机观测：CPU0 产块 relay → CPU1 bmp_motor_observer
 */
#include "mp_relay_algo_demo.h"

const mp_relay_algo_params_t g_mp_relay_algo_params = {
    .tag = "dual_core_motor",
    .pass_label = "DUAL_CORE_MOTOR",
    .wdg_name = "dual_motor",
    .kind = MP_RELAY_ALGO_MOTOR,
    .sample_rate_hz = 1000.0f,
    .signal_freq_hz = 120.0f,
    .samples_per_block = 1u,
    .block_period_us = 4000u,
    .pass_blocks = 10u,
};

int main(void) {
    return mp_relay_algo_demo_main();
}
