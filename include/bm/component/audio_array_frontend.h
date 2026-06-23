/**
 * @file audio_array_frontend.h
 * @brief 麦克风阵列前端：延迟对齐与 DAS/MVDR 波束成形
 *
 * 支持最多 4 通道；可配置固定 delay_samples 或通过 GCC-PHAT 估计时延。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.3
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始骨架
 * 2026-06-17       0.2            zeh            DAS/MVDR 波束模式
 * 2026-06-23       0.3            zeh            补 SPDX 与函数级 Doxygen
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_AUDIO_ARRAY_FRONTEND_H
#define BM_AUDIO_ARRAY_FRONTEND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 单组件最大支持麦克风通道数 */
#define BM_AUDIO_ARRAY_MAX_CHANNELS  4u

/**
 * @brief 波束成形模式枚举
 *
 * - BM_AUDIO_BEAM_DAS  : 延迟-求和（Delay-and-Sum），计算量小
 * - BM_AUDIO_BEAM_MVDR : 最小方差无失真响应，空间抑制能力更强
 */
typedef enum {
    BM_AUDIO_BEAM_DAS = 0, /**< 延迟求和波束成形 */
    BM_AUDIO_BEAM_MVDR     /**< MVDR 自适应波束成形 */
} bm_audio_beam_mode_t;

/**
 * @brief 麦克风阵列前端静态配置
 */
typedef struct {
    uint32_t num_channels;           /**< 实际通道数，须 ≤ BM_AUDIO_ARRAY_MAX_CHANNELS */
    uint32_t block_samples;          /**< 每帧采样点数 */
    float    sample_hz;              /**< 采样率（Hz），须 > 0 */
    int      use_fixed_delay;        /**< 非零：使用 fixed_delay_samples；零：GCC-PHAT 自动估计 */
    int32_t  fixed_delay_samples[BM_AUDIO_ARRAY_MAX_CHANNELS]; /**< 各通道固定延迟（采样点） */
    int32_t  max_gcc_lag;            /**< GCC-PHAT 最大搜索滞后（采样点），use_fixed_delay=0 时有效 */
    bm_audio_beam_mode_t beam_mode;  /**< 波束成形模式 */
    float    mvdr_diagonal_load;     /**< MVDR 对角加载量；≤0 时内部取 1e-3 */
} bm_audio_array_frontend_config_t;

/**
 * @brief 麦克风阵列前端遥测快照
 */
typedef struct {
    uint32_t sequence;                                   /**< 步计数（单调递增） */
    float    energy;                                     /**< 本帧波束输出均方能量 */
    int32_t  delay_samples[BM_AUDIO_ARRAY_MAX_CHANNELS]; /**< 本帧各通道实际延迟 */
} bm_audio_array_frontend_telemetry_t;

/**
 * @brief 麦克风阵列前端运行时状态
 *
 * 由 bm_audio_array_frontend_init() 绑定外部缓冲区后使用；
 * 不得在运行时直接修改（通过 API 操作）。
 */
typedef struct {
    float   *beam_buffer;                                /**< 波束成形中间缓冲区（外部分配） */
    uint32_t beam_buffer_len;                            /**< beam_buffer 元素容量 */
    float   *gcc_work;                                   /**< GCC-PHAT 工作缓冲区（外部分配） */
    uint32_t gcc_work_count;                             /**< gcc_work 元素容量 */
    int32_t  active_delays[BM_AUDIO_ARRAY_MAX_CHANNELS]; /**< 当前帧各通道实际延迟 */
    float    last_energy;                                /**< 上帧波束输出均方能量 */
    uint32_t step_count;                                 /**< 已处理帧数 */
    bm_audio_array_frontend_telemetry_t telemetry;       /**< 最新遥测快照 */
} bm_audio_array_frontend_state_t;

/**
 * @brief 麦克风阵列前端完整实例（配置 + 状态）
 */
typedef struct {
    bm_audio_array_frontend_config_t config; /**< 静态配置，初始化前填写 */
    bm_audio_array_frontend_state_t  state;  /**< 运行时状态，由 API 维护 */
} bm_audio_array_frontend_axis_t;

/**
 * @brief 校验阵列前端配置合法性
 *
 * 检查通道数、帧长、采样率及 GCC-PHAT 参数范围。
 *
 * @param config 指向待校验的配置结构体，不可为 NULL
 * @return BM_OK 配置合法；BM_ERR_INVALID 参数越界或指针为空
 */
int  bm_audio_array_frontend_validate_config(
    const bm_audio_array_frontend_config_t *config);

/**
 * @brief 绑定外部缓冲区并初始化阵列前端
 *
 * 在调用任何 step 函数前必须先调用本函数。绑定 beam_buffer 与
 * gcc_work（use_fixed_delay=0 时必须提供），校验配置并复位状态。
 *
 * @param axis             实例指针，config 须已填写
 * @param beam_buffer      波束成形中间缓冲区，容量须 ≥ block_samples
 * @param beam_buffer_len  beam_buffer 元素容量
 * @param gcc_work         GCC-PHAT 工作缓冲区（use_fixed_delay=0 时必须非 NULL）
 * @param gcc_work_count   gcc_work 元素容量
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法或缓冲区容量不足
 */
int  bm_audio_array_frontend_init(bm_audio_array_frontend_axis_t *axis,
                                  float *beam_buffer,
                                  uint32_t beam_buffer_len,
                                  float *gcc_work,
                                  uint32_t gcc_work_count);

/**
 * @brief 复位阵列前端状态（不释放缓冲区绑定）
 *
 * 清零延迟、能量与步计数；beam_buffer/gcc_work 绑定保持不变。
 *
 * @param axis 实例指针；为 NULL 时静默返回
 */
void bm_audio_array_frontend_reset(bm_audio_array_frontend_axis_t *axis);

/**
 * @brief 执行一帧麦克风阵列波束成形
 *
 * 按配置更新各通道延迟（固定或 GCC-PHAT 自动估计），
 * 执行 DAS 或 MVDR 波束成形，结果写入 mono_out。
 * 同步更新 state.telemetry。
 *
 * @param axis     实例指针
 * @param channels 各通道 PCM 帧数组，每路长度须 ≥ block_samples
 * @param mono_out 输出单声道缓冲区
 * @param out_cap  mono_out 元素容量，须 ≥ block_samples
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法或配置不合法
 */
int  bm_audio_array_frontend_step(bm_audio_array_frontend_axis_t *axis,
                                  const float *channels[BM_AUDIO_ARRAY_MAX_CHANNELS],
                                  float *mono_out,
                                  uint32_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* BM_AUDIO_ARRAY_FRONTEND_H */
