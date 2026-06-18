/**
 * @file bm_algo_comm.h
 * @brief 通信 DSP：CRC、DTMF 检测（Goertzel 组）
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-17       1.1            zeh            2-FSK 解调与 Hamming(7,4)
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_ALGO_COMM_H
#define BM_ALGO_COMM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t bm_algo_crc16_ccitt(const uint8_t *data, uint32_t len, uint16_t init);
uint32_t bm_algo_crc32(const uint8_t *data, uint32_t len, uint32_t init);

#define BM_ALGO_DTMF_ROW_COUNT 4u
#define BM_ALGO_DTMF_COL_COUNT 4u

typedef struct {
    float sample_hz;
    uint32_t block_size;
} bm_algo_dtmf_config_t;

typedef struct {
    float row_energy[BM_ALGO_DTMF_ROW_COUNT];
    float col_energy[BM_ALGO_DTMF_COL_COUNT];
} bm_algo_dtmf_state_t;

void bm_algo_dtmf_reset(bm_algo_dtmf_state_t *state);
int bm_algo_dtmf_detect(bm_algo_dtmf_state_t *state,
                        const bm_algo_dtmf_config_t *config,
                        const float *samples,
                        uint32_t n,
                        char *symbol_out);

/* ---------- 2-FSK 非相干解调（E1：Goertzel 能量比） ---------- */
typedef struct {
    float sample_hz;
    float mark_hz;
    float space_hz;
    float bit_rate_hz;
} bm_algo_fsk2_config_t;

/**
 * @brief 2-FSK 块解调为比特流
 *
 * @param samples 输入样本（可为 NULL）
 * @param n 样本数
 * @param config mark/space 频率与比特率
 * @param bits_out 输出比特（每字节 1 位，LSB 优先）
 * @param max_bits 输出缓冲容量（比特数）
 * @return 写入比特数；参数无效返回 -1
 */
int bm_algo_fsk2_demod_block(const float *samples,
                             uint32_t n,
                             const bm_algo_fsk2_config_t *config,
                             uint8_t *bits_out,
                             uint32_t max_bits);

/* ---------- Hamming(7,4) 轻量 FEC ---------- */
/** 4 位数据 → 7 位码字（低 7 位有效） */
uint8_t bm_algo_hamming74_encode(uint8_t data_nibble);

/** 7 位码字 → 4 位数据（纠 1 bit） */
uint8_t bm_algo_hamming74_decode(uint8_t code);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_COMM_H */
