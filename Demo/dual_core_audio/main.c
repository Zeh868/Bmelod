/**
 * @file main.c
 * @brief 双核音频 AGC：CPU0 产块 relay → CPU1 bmp_audio_agc
 */
#include "mp_relay_algo_demo.h"

const mp_relay_algo_params_t g_mp_relay_algo_params = {
    .tag = "dual_core_audio",
    .pass_label = "DUAL_CORE_AUDIO",
    .wdg_name = "dual_audio",
    .kind = MP_RELAY_ALGO_AUDIO,
    .sample_rate_hz = 16000.0f,
    .signal_freq_hz = 440.0f,
    .samples_per_block = 64u,
    .block_period_us = 4000u,
    .pass_blocks = 10u,
};

int main(void) {
    return mp_relay_algo_demo_main();
}
