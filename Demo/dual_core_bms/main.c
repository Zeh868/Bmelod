/**
 * @file main.c
 * @brief 双核 BMS：CPU0 产块 relay → CPU1 bmp_bms_fusion
 */
#include "mp_relay_algo_demo.h"

const mp_relay_algo_params_t g_mp_relay_algo_params = {
    .tag = "dual_core_bms",
    .pass_label = "DUAL_CORE_BMS",
    .wdg_name = "dual_bms",
    .kind = MP_RELAY_ALGO_BMS,
    .sample_rate_hz = 1000.0f,
    .signal_freq_hz = 0.0f,
    .samples_per_block = 2u,
    .block_period_us = 4000u,
    .pass_blocks = 10u,
};

int main(void) {
    return mp_relay_algo_demo_main();
}
