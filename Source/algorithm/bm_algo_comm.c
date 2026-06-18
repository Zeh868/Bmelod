/**
 * @file bm_algo_comm.c
 * @brief 通信 DSP：CRC 与 DTMF 检测实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_comm.h"
#include "bm/algorithm/bm_algo_spectral.h"
#include <stddef.h>

#include <math.h>

static const float s_dtmf_rows[BM_ALGO_DTMF_ROW_COUNT] = {
    697.0f, 770.0f, 852.0f, 941.0f
};
static const float s_dtmf_cols[BM_ALGO_DTMF_COL_COUNT] = {
    1209.0f, 1336.0f, 1477.0f, 1633.0f
};
static const char s_dtmf_map[BM_ALGO_DTMF_ROW_COUNT][BM_ALGO_DTMF_COL_COUNT] = {
    { '1', '2', '3', 'A' },
    { '4', '5', '6', 'B' },
    { '7', '8', '9', 'C' },
    { '*', '0', '#', 'D' }
};

uint16_t bm_algo_crc16_ccitt(const uint8_t *data, uint32_t len, uint16_t init) {
    uint32_t i;
    uint32_t crc = init;

    if (data == NULL) {
        return (uint16_t)crc;
    }

    for (i = 0u; i < len; ++i) {
        uint32_t b;
        crc ^= (uint32_t)data[i] << 8;
        for (b = 0u; b < 8u; ++b) {
            if (crc & 0x8000u) {
                crc = ((crc << 1) ^ 0x1021u) & 0xffffu;
            } else {
                crc = (crc << 1) & 0xffffu;
            }
        }
    }
    return (uint16_t)crc;
}

uint32_t bm_algo_crc32(const uint8_t *data, uint32_t len, uint32_t init) {
    uint32_t i;
    uint32_t crc = init;
    static const uint32_t poly = 0xEDB88320u;

    if (data == NULL) {
        return crc;
    }

    for (i = 0u; i < len; ++i) {
        uint32_t b;
        crc ^= data[i];
        for (b = 0u; b < 8u; ++b) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ poly;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

void bm_algo_dtmf_reset(bm_algo_dtmf_state_t *state) {
    uint32_t i;

    if (state == NULL) {
        return;
    }
    for (i = 0u; i < BM_ALGO_DTMF_ROW_COUNT; ++i) {
        state->row_energy[i] = 0.0f;
    }
    for (i = 0u; i < BM_ALGO_DTMF_COL_COUNT; ++i) {
        state->col_energy[i] = 0.0f;
    }
}

static float goertzel_energy(const float *samples, uint32_t n,
                             float freq_hz, float sample_hz) {
    bm_algo_goertzel_config_t cfg;
    bm_algo_goertzel_state_t st;
    uint32_t i;

    cfg.target_freq_hz = freq_hz;
    cfg.sample_hz = sample_hz;
    cfg.block_size = n;
    bm_algo_goertzel_init(&st, &cfg);

    for (i = 0u; i < n; ++i) {
        bm_algo_goertzel_feed(&st, &cfg, samples[i]);
    }
    return bm_algo_goertzel_result(&st, &cfg);
}

int bm_algo_dtmf_detect(bm_algo_dtmf_state_t *state,
                        const bm_algo_dtmf_config_t *config,
                        const float *samples,
                        uint32_t n,
                        char *symbol_out) {
    uint32_t ri;
    uint32_t ci;
    uint32_t best_r = 0u;
    uint32_t best_c = 0u;
    float max_r = 0.0f;
    float max_c = 0.0f;

    if (state == NULL || config == NULL || samples == NULL ||
        symbol_out == NULL || n == 0u) {
        return -1;
    }

    for (ri = 0u; ri < BM_ALGO_DTMF_ROW_COUNT; ++ri) {
        state->row_energy[ri] = goertzel_energy(samples, n,
                                                s_dtmf_rows[ri],
                                                config->sample_hz);
        if (state->row_energy[ri] > max_r) {
            max_r = state->row_energy[ri];
            best_r = ri;
        }
    }

    for (ci = 0u; ci < BM_ALGO_DTMF_COL_COUNT; ++ci) {
        state->col_energy[ci] = goertzel_energy(samples, n,
                                                s_dtmf_cols[ci],
                                                config->sample_hz);
        if (state->col_energy[ci] > max_c) {
            max_c = state->col_energy[ci];
            best_c = ci;
        }
    }

    if (max_r < 1e-4f || max_c < 1e-4f) {
        *symbol_out = '\0';
        return 0;
    }

    *symbol_out = s_dtmf_map[best_r][best_c];
    return 1;
}

int bm_algo_fsk2_demod_block(const float *samples,
                             uint32_t n,
                             const bm_algo_fsk2_config_t *config,
                             uint8_t *bits_out,
                             uint32_t max_bits) {
    uint32_t samples_per_bit;
    uint32_t bit_idx;
    uint32_t offset;

    if (samples == NULL || config == NULL || bits_out == NULL ||
        config->sample_hz <= 0.0f || config->bit_rate_hz <= 0.0f ||
        config->mark_hz <= 0.0f || config->space_hz <= 0.0f ||
        n == 0u || max_bits == 0u) {
        return -1;
    }

    samples_per_bit = (uint32_t)(config->sample_hz / config->bit_rate_hz + 0.5f);
    if (samples_per_bit == 0u) {
        return -1;
    }

    bit_idx = 0u;
    for (offset = 0u;
         offset + samples_per_bit <= n && bit_idx < max_bits;
         offset += samples_per_bit) {
        float e_mark;
        float e_space;

        e_mark = goertzel_energy(samples + offset, samples_per_bit,
                                 config->mark_hz, config->sample_hz);
        e_space = goertzel_energy(samples + offset, samples_per_bit,
                                  config->space_hz, config->sample_hz);
        bits_out[bit_idx] = (e_mark >= e_space) ? 1u : 0u;
        bit_idx++;
    }

    return (int)bit_idx;
}

uint8_t bm_algo_hamming74_encode(uint8_t data_nibble) {
    uint8_t d0 = data_nibble & 1u;
    uint8_t d1 = (data_nibble >> 1) & 1u;
    uint8_t d2 = (data_nibble >> 2) & 1u;
    uint8_t d3 = (data_nibble >> 3) & 1u;
    uint8_t p1 = d0 ^ d1 ^ d3;
    uint8_t p2 = d0 ^ d2 ^ d3;
    uint8_t p4 = d1 ^ d2 ^ d3;

    return (uint8_t)(p1 | (p2 << 1) | (d0 << 2) | (p4 << 3) |
                     (d1 << 4) | (d2 << 5) | (d3 << 6));
}

uint8_t bm_algo_hamming74_decode(uint8_t code) {
    uint8_t b1 = code & 1u;
    uint8_t b2 = (code >> 1) & 1u;
    uint8_t b3 = (code >> 2) & 1u;
    uint8_t b4 = (code >> 3) & 1u;
    uint8_t b5 = (code >> 4) & 1u;
    uint8_t b6 = (code >> 5) & 1u;
    uint8_t b7 = (code >> 6) & 1u;
    uint8_t s1 = b1 ^ b3 ^ b5 ^ b7;
    uint8_t s2 = b2 ^ b3 ^ b6 ^ b7;
    uint8_t s4 = b4 ^ b5 ^ b6 ^ b7;
    uint8_t syndrome = (uint8_t)(s4 << 2 | s2 << 1 | s1);
    uint8_t corrected = code;

    if (syndrome != 0u && syndrome <= 7u) {
        corrected ^= (uint8_t)(1u << (syndrome - 1u));
        b3 = (corrected >> 2) & 1u;
        b5 = (corrected >> 4) & 1u;
        b6 = (corrected >> 5) & 1u;
        b7 = (corrected >> 6) & 1u;
    }

    return (uint8_t)(b3 | (b5 << 1) | (b6 << 2) | (b7 << 3));
}
