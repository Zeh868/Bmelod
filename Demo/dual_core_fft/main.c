/**
 * @file main.c
 * @brief 双核 FFT：CPU0 产块 relay → CPU1 bmp_fft_enhanced
 */
#include "mp_relay_algo_demo.h"

const mp_relay_algo_params_t g_mp_relay_algo_params = {
    .tag = "dual_core_fft",
    .pass_label = "DUAL_CORE_FFT",
    .wdg_name = "dual_fft",
    .kind = MP_RELAY_ALGO_FFT,
    .sample_rate_hz = 16000.0f,
    .signal_freq_hz = 1000.0f,
    .samples_per_block = 64u,
    .block_period_us = 4000u,
    .pass_blocks = 10u,
};

int main(void) {
    return mp_relay_algo_demo_main();
}
