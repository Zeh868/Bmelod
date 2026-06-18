/**
 * @file audio_array_frontend.h
 * @brief 麦克风阵列前端：延迟对齐与 DAS/MVDR 波束成形
 *
 * 支持最多 4 通道；可配置固定 delay_samples 或通过 GCC-PHAT 估计时延。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始骨架
 * 2026-06-17       0.2            zeh            DAS/MVDR 波束模式
 */
#ifndef BM_AUDIO_ARRAY_FRONTEND_H
#define BM_AUDIO_ARRAY_FRONTEND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_AUDIO_ARRAY_MAX_CHANNELS  4u

typedef enum {
    BM_AUDIO_BEAM_DAS = 0,
    BM_AUDIO_BEAM_MVDR
} bm_audio_beam_mode_t;

typedef struct {
    uint32_t num_channels;
    uint32_t block_samples;
    float    sample_hz;
    int      use_fixed_delay;
    int32_t  fixed_delay_samples[BM_AUDIO_ARRAY_MAX_CHANNELS];
    int32_t  max_gcc_lag;
    bm_audio_beam_mode_t beam_mode;
    float    mvdr_diagonal_load;
} bm_audio_array_frontend_config_t;

typedef struct {
    uint32_t sequence;
    float    energy;
    int32_t  delay_samples[BM_AUDIO_ARRAY_MAX_CHANNELS];
} bm_audio_array_frontend_telemetry_t;

typedef struct {
    float   *beam_buffer;
    uint32_t beam_buffer_len;
    float   *gcc_work;
    uint32_t gcc_work_count;
    int32_t  active_delays[BM_AUDIO_ARRAY_MAX_CHANNELS];
    float    last_energy;
    uint32_t step_count;
    bm_audio_array_frontend_telemetry_t telemetry;
} bm_audio_array_frontend_state_t;

typedef struct {
    bm_audio_array_frontend_config_t config;
    bm_audio_array_frontend_state_t  state;
} bm_audio_array_frontend_axis_t;

int  bm_audio_array_frontend_validate_config(
    const bm_audio_array_frontend_config_t *config);
int  bm_audio_array_frontend_init(bm_audio_array_frontend_axis_t *axis,
                                  float *beam_buffer,
                                  uint32_t beam_buffer_len,
                                  float *gcc_work,
                                  uint32_t gcc_work_count);
void bm_audio_array_frontend_reset(bm_audio_array_frontend_axis_t *axis);
int  bm_audio_array_frontend_step(bm_audio_array_frontend_axis_t *axis,
                                  const float *channels[BM_AUDIO_ARRAY_MAX_CHANNELS],
                                  float *mono_out,
                                  uint32_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* BM_AUDIO_ARRAY_FRONTEND_H */
