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
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_ALGO_COMM_H
#define BM_ALGO_COMM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 计算 CRC-16/CCITT（多项式 0x1021，大端位序）
 *
 * @param data 输入数据缓冲区指针，为 NULL 时返回 init
 * @param len  数据字节数
 * @param init CRC 初始值（常用 0x0000 或 0xFFFF）
 * @return 计算得到的 16 位 CRC 值
 */
uint16_t bm_algo_crc16_ccitt(const uint8_t *data, uint32_t len, uint16_t init);

/**
 * @brief 计算 CRC-32（多项式 0xEDB88320，小端/反射位序，兼容 IEEE 802.3）
 *
 * @param data 输入数据缓冲区指针，为 NULL 时返回 init
 * @param len  数据字节数
 * @param init CRC 初始值（常用 0xFFFFFFFF）
 * @return 计算得到的 32 位 CRC 值（未取反，如需标准 CRC-32 请调用方自行异或 0xFFFFFFFF）
 */
uint32_t bm_algo_crc32(const uint8_t *data, uint32_t len, uint32_t init);

/** DTMF 行频数量（697/770/852/941 Hz） */
#define BM_ALGO_DTMF_ROW_COUNT 4u
/** DTMF 列频数量（1209/1336/1477/1633 Hz） */
#define BM_ALGO_DTMF_COL_COUNT 4u

/**
 * @brief DTMF 检测配置参数
 */
typedef struct {
    float    sample_hz;  /**< 输入信号采样率（Hz） */
    uint32_t block_size; /**< 每次检测的样本块大小 */
} bm_algo_dtmf_config_t;

/**
 * @brief DTMF 检测状态（各频率 Goertzel 能量缓存）
 */
typedef struct {
    float row_energy[BM_ALGO_DTMF_ROW_COUNT]; /**< 四个行频的 Goertzel 能量 */
    float col_energy[BM_ALGO_DTMF_COL_COUNT]; /**< 四个列频的 Goertzel 能量 */
} bm_algo_dtmf_state_t;

/**
 * @brief 复位 DTMF 检测状态，清零所有能量缓存
 *
 * @param state 状态指针，为 NULL 时静默返回
 */
void bm_algo_dtmf_reset(bm_algo_dtmf_state_t *state);

/**
 * @brief 对一块样本执行 DTMF 单音检测
 *
 * 内部使用 Goertzel 算法分别计算行频与列频能量，取能量最大的行列组合输出符号。
 * 行列能量均低于 1e-4 时输出 '\0' 并返回 0（无音）。
 *
 * @param state      检测状态指针（存储各频率能量）
 * @param config     检测配置指针（采样率）
 * @param samples    输入浮点样本数组
 * @param n          样本数
 * @param symbol_out 输出符号（'0'~'9'、'A'~'D'、'*'、'#'；无音时为 '\0'）
 * @return 1 表示检测到符号；0 表示无音；-1 表示参数无效
 */
int bm_algo_dtmf_detect(bm_algo_dtmf_state_t *state,
                        const bm_algo_dtmf_config_t *config,
                        const float *samples,
                        uint32_t n,
                        char *symbol_out);

/* ---------- 2-FSK 非相干解调（E1：Goertzel 能量比） ---------- */

/**
 * @brief 2-FSK 解调配置参数
 */
typedef struct {
    float sample_hz;   /**< 输入信号采样率（Hz） */
    float mark_hz;     /**< 标记频率（逻辑 1 对应的载波频率，Hz） */
    float space_hz;    /**< 空号频率（逻辑 0 对应的载波频率，Hz） */
    float bit_rate_hz; /**< 比特率（bps） */
} bm_algo_fsk2_config_t;

/**
 * @brief 2-FSK 块解调为比特流
 *
 * 按 sample_hz/bit_rate_hz 对样本分段，对每段用 Goertzel 分别计算
 * mark 与 space 能量，能量较大者决定比特值（1 或 0）。
 *
 * @param samples  输入样本数组指针，为 NULL 时返回 -1
 * @param n        样本总数
 * @param config   FSK 配置指针，为 NULL 时返回 -1
 * @param bits_out 输出比特缓冲区（每字节存 1 比特，值为 0 或 1）
 * @param max_bits 输出缓冲区最大容量（比特数）
 * @return 写入的比特数；参数无效返回 -1
 */
int bm_algo_fsk2_demod_block(const float *samples,
                             uint32_t n,
                             const bm_algo_fsk2_config_t *config,
                             uint8_t *bits_out,
                             uint32_t max_bits);

/* ---------- Hamming(7,4) 轻量 FEC ---------- */

/**
 * @brief Hamming(7,4) 编码：4 位数据 → 7 位码字（低 7 位有效）
 *
 * @param data_nibble 输入数据半字节（仅低 4 位有效）
 * @return 7 位 Hamming 码字（低 7 位有效，bit7 = 0）
 */
uint8_t bm_algo_hamming74_encode(uint8_t data_nibble);

/**
 * @brief Hamming(7,4) 解码：7 位码字 → 4 位数据（纠 1 bit 错误）
 *
 * 通过综合码（syndrome）定位并纠正单比特错误，返回原始 4 位数据。
 * 双比特错误无法检测（Hamming(7,4) 能力限制）。
 *
 * @param code 接收到的 7 位码字（低 7 位有效）
 * @return 解码（并纠错）后的 4 位数据（低 4 位有效）
 */
uint8_t bm_algo_hamming74_decode(uint8_t code);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_COMM_H */
