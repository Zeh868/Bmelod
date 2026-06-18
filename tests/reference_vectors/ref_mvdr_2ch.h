/**
 * @file ref_mvdr_2ch.h
 * @brief 2 通道 MVDR 黄金参考向量
 *
 * 1 kHz 正弦、ch1 相对 ch0 延迟 4 采样；对齐后 MVDR 输出能量阈值与峰值位置。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            初始黄金向量
 */
#ifndef REF_MVDR_2CH_H
#define REF_MVDR_2CH_H

#include <stdint.h>

#define REF_MVDR_2CH_BLOCK_SAMPLES   32u
#define REF_MVDR_2CH_SAMPLE_HZ       8000.0f
#define REF_MVDR_2CH_DELAY_CH1       4
#define REF_MVDR_2CH_DIAGONAL_LOAD   1e-3f

/** 对齐后 MVDR 块能量下限 */
#define REF_MVDR_2CH_ENERGY_MIN      0.05f

/** 期望峰值出现在块内后半段（对齐后正弦已建立） */
#define REF_MVDR_2CH_PEAK_INDEX_MIN  8u

#endif /* REF_MVDR_2CH_H */
