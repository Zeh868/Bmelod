/**
 * @file mp_relay_algo_demo.h
 * @brief 双核 relay + bmp_algo 工业算法 Demo 公共框架
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-16
 *
 * @par 修改日志:
 *
 * Date       Version Author Description
 * 2026-06-16 1.0     zeh    首版：CPU0 产块 relay → CPU1 bmp_*
 *
 */
#ifndef MP_RELAY_ALGO_DEMO_H
#define MP_RELAY_ALGO_DEMO_H

#include <stdint.h>

/** 工业算法种类 */
typedef enum {
    MP_RELAY_ALGO_FFT = 0,
    MP_RELAY_ALGO_VIBRATION = 1,
    MP_RELAY_ALGO_BMS = 2,
    MP_RELAY_ALGO_MOTOR = 3,
    MP_RELAY_ALGO_AUDIO = 4
} mp_relay_algo_kind_t;

/** Demo 运行参数（各示例 main.c 填充） */
typedef struct {
    const char           *tag;
    const char           *pass_label;
    const char           *wdg_name;
    mp_relay_algo_kind_t kind;
    float                sample_rate_hz;
    float                signal_freq_hz;
    uint32_t             samples_per_block;
    uint32_t             block_period_us;
    uint32_t             pass_blocks;
} mp_relay_algo_params_t;

/** 各示例导出的参数表 */
extern const mp_relay_algo_params_t g_mp_relay_algo_params;

/**
 * @brief 双核 relay 算法 Demo 主入口
 *
 * @return 0 验收通过；1 失败
 */
int mp_relay_algo_demo_main(void);

#endif /* MP_RELAY_ALGO_DEMO_H */
