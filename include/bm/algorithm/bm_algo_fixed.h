/**
 * @file bm_algo_fixed.h
 * @brief 定点算法核：Q31/Q15 显式后缀 API（与 float 并存）
 *
 * Q31：1.0 ≈ 0x7FFFFFFF；Q15：1.0 = 32767。系数与信号均按 ±1.0 归一化。
 * 与 float 核分文件、分符号，不使用全局 typedef 在编译期切换 ABI。
 *
 * @warning float 后端（S3）：以下 Q15/Q31 API 族**内部以 float 实现**
 *          （Q→float→算→float→Q 往返，经对应 `bm_algo_*_step` 桥接）：
 *          differentiator、scurve、DDA、complementary、madgwick、mahony、
 *          flux_observer、sogi_pll、smith_predictor、linear_resampler。
 *          同族 `rms_q15`/`rms_q31` 内部使用 double `sqrt`。
 *          后果：FPU-less 平台有性能开销，且**不保证跨平台位精确复现**
 *          （与"定点=可位精确复现"的确定性预期不符）。真定点化为后续项。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 2.6
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-17       1.1            zeh            Q31 斜坡与 S 曲线
 * 2026-06-17       1.2            zeh            Q15 滑动平均/PID 与 Q31 迟滞
 * 2026-06-17       1.3            zeh            Q31 梯形速度曲线
 * 2026-06-17       1.4            zeh            Q31 LPF、Q15 中值/高通
 * 2026-06-17       1.5            zeh            Q31 微分器、Q15 包络/RMS
 * 2026-06-17       1.6            zeh            定点第七批：库仑/高通/滑动平均 Q31、死区/滞回 Q15
 * 2026-06-17       1.7            zeh            定点第八批：积分/限速/超前滞后 Q15、二阶 IIR Q31
 * 2026-06-17       1.8            zeh            定点第九批：DOB Q15、包络/RMS Q31、背隙逆补偿 Q31
 * 2026-06-17       1.9            zeh            定点第十批：微分/DOB/库仑/超前滞后/互补/前馈 Q15/Q31
 * 2026-06-17       2.0            zeh            定点第十一批：PI/PR/斜坡/梯形/冗余/速率/SOC 融合 Q15/Q31
 * 2026-06-17       2.1            zeh            定点第十二批：S 曲线/MPPT/信号质量/Wh 积分 Q15/Q31
 * 2026-06-17       2.2            zeh            定点第十四批：全族 Q31/Q15 后缀 API 收口
 * 2026-06-23       2.3            zeh            缺陷修复：Mahony Q15/Q31 state 新增 Ki 积分持久化字段
 * 2026-06-23       2.4            zeh            磁链观测器 Q15/Q31 配置结构体新增 wc_rad_s 衰减截止频率字段；修正 BM_ALGO_SQRT3_Q31 为精确 Q30 值
 * 2026-07-16       2.5            zeh            SOGI-PLL Q15/Q31 state 新增
 *                                                d_alpha_prev/d_beta_prev Tustin
 *                                                导数缓存持久化字段（与 float 版对齐）
 *
 * 2026-07-28       2.6            zeh            Initialization status documents BM_OK/BM_ERR_*
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_ALGO_FIXED_H
#define BM_ALGO_FIXED_H

#include "bm/algorithm/bm_algo_errors.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t bm_algo_q31_t;
typedef int16_t bm_algo_q15_t;

#define BM_ALGO_Q31_ONE   ((bm_algo_q31_t)2147483647)
#define BM_ALGO_Q15_ONE   ((bm_algo_q15_t)32767)

/**
 * @brief 将 Q1.31 值钳位到指定闭区间
 *
 * @param value 输入值
 * @param min_v 下限
 * @param max_v 上限
 * @return 钳位后的 Q1.31 值
 */
bm_algo_q31_t bm_algo_clamp_q31(bm_algo_q31_t value,
                                bm_algo_q31_t min_v,
                                bm_algo_q31_t max_v);

/**
 * @brief 将 Q1.15 值钳位到指定闭区间
 *
 * @param value 输入值
 * @param min_v 下限
 * @param max_v 上限
 * @return 钳位后的 Q1.15 值
 */
bm_algo_q15_t bm_algo_clamp_q15(bm_algo_q15_t value,
                                bm_algo_q15_t min_v,
                                bm_algo_q15_t max_v);

/**
 * @brief 将浮点数转换为 Q1.31
 *
 * @param value 浮点输入
 * @return 截断量化后的 Q1.31 值；越界或无穷输入饱和，NaN 返回 0
 */
bm_algo_q31_t bm_algo_float_to_q31(float value);

/**
 * @brief 将 Q1.31 值转换为浮点数
 *
 * @param value Q1.31 输入
 * @return value/2^31
 */
float bm_algo_q31_to_float(bm_algo_q31_t value);

/**
 * @brief 将浮点数转换为 Q1.15
 *
 * @param value 浮点输入
 * @return 截断量化后的 Q1.15 值；越界或无穷输入饱和，NaN 返回 0
 */
bm_algo_q15_t bm_algo_float_to_q15(float value);

/**
 * @brief 将 Q1.15 值转换为浮点数
 *
 * @param value Q1.15 输入
 * @return value/2^15
 */
float bm_algo_q15_to_float(bm_algo_q15_t value);

typedef struct {
    bm_algo_q31_t kp;
    bm_algo_q31_t ki;
    bm_algo_q31_t out_min;
    bm_algo_q31_t out_max;
    bm_algo_q31_t integrator_min;
    bm_algo_q31_t integrator_max;
} bm_algo_pi_q31_config_t;

typedef struct {
    bm_algo_q31_t integrator;
    bm_algo_q31_t output;
} bm_algo_pi_q31_state_t;

/**
 * @brief 复位 Q1.31 PI 控制器状态
 *
 * @param state 状态；NULL 时静默返回
 * @param output 初始输出
 */
void bm_algo_pi_q31_reset(bm_algo_pi_q31_state_t *state, bm_algo_q31_t output);

/**
 * @brief 执行一拍 Q1.31 PI 控制并进行积分与输出限幅
 *
 * @param state 控制器状态
 * @param config PI 参数及限幅
 * @param error 本拍误差
 * @param dt_q31 Q1.31 时间步长，必须大于 0
 * @return 限幅后的 Q1.31 输出；参数无效或时间步长非正时返回 0
 */
bm_algo_q31_t bm_algo_pi_q31_step(bm_algo_pi_q31_state_t *state,
                                  const bm_algo_pi_q31_config_t *config,
                                  bm_algo_q31_t error,
                                  bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q15_t alpha_q15;
} bm_algo_lpf1_q15_config_t;

typedef struct {
    bm_algo_q15_t output;
} bm_algo_lpf1_q15_state_t;

/**
 * @brief 复位 Q1.15 一阶低通滤波器
 *
 * @param state 滤波状态；NULL 时静默返回
 * @param output 初始输出
 */
void bm_algo_lpf1_q15_reset(bm_algo_lpf1_q15_state_t *state, bm_algo_q15_t output);

/**
 * @brief 执行一拍 Q1.15 一阶低通滤波
 *
 * @param state 滤波状态
 * @param config 含 Q1.15 平滑系数的配置
 * @param input 本拍输入
 * @return Q1.15 滤波输出；state 或 config 为 NULL 时原样返回 input
 */
bm_algo_q15_t bm_algo_lpf1_q15_step(bm_algo_lpf1_q15_state_t *state,
                                    const bm_algo_lpf1_q15_config_t *config,
                                    bm_algo_q15_t input);

typedef struct {
    bm_algo_q31_t kp;
    bm_algo_q31_t ki;
    bm_algo_q31_t kd;
    bm_algo_q31_t out_min;
    bm_algo_q31_t out_max;
    bm_algo_q31_t integrator_min;
    bm_algo_q31_t integrator_max;
    bm_algo_q31_t d_filter_alpha_q31;
} bm_algo_pid_q31_config_t;

typedef struct {
    bm_algo_q31_t integrator;
    bm_algo_q31_t prev_error;
    bm_algo_q31_t d_filtered;
    bm_algo_q31_t output;
} bm_algo_pid_q31_state_t;

/**
 * @brief 复位 Q1.31 PID 控制器状态
 *
 * @param state 状态；NULL 时静默返回
 * @param output 初始输出
 */
void bm_algo_pid_q31_reset(bm_algo_pid_q31_state_t *state, bm_algo_q31_t output);

/**
 * @brief 执行一拍带微分滤波和限幅的 Q1.31 PID 控制
 *
 * @param state 控制器状态
 * @param config PID 参数及限幅
 * @param error 本拍误差
 * @param dt_q31 Q1.31 时间步长，必须大于 0
 * @return 限幅后的 Q1.31 输出；参数无效或时间步长非正时返回 0
 */
bm_algo_q31_t bm_algo_pid_q31_step(bm_algo_pid_q31_state_t *state,
                                   const bm_algo_pid_q31_config_t *config,
                                   bm_algo_q31_t error,
                                   bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q15_t b0;
    bm_algo_q15_t b1;
    bm_algo_q15_t b2;
    bm_algo_q15_t a1;
    bm_algo_q15_t a2;
} bm_algo_biquad_q15_config_t;

typedef struct {
    bm_algo_q15_t z1;
    bm_algo_q15_t z2;
} bm_algo_biquad_q15_state_t;

/**
 * @brief 清零 Q1.15 双二阶滤波器延迟状态
 *
 * @param state 滤波状态；NULL 时静默返回
 */
void bm_algo_biquad_q15_reset(bm_algo_biquad_q15_state_t *state);

/**
 * @brief 执行一拍 Q1.15 双二阶滤波并饱和输出
 *
 * @param state 滤波状态
 * @param config Q1.15 滤波系数
 * @param input 本拍输入
 * @return Q1.15 滤波输出；state 或 config 为 NULL 时原样返回 input
 */
bm_algo_q15_t bm_algo_biquad_q15_step(bm_algo_biquad_q15_state_t *state,
                                      const bm_algo_biquad_q15_config_t *config,
                                      bm_algo_q15_t input);

/* ---------- 电机控制 Q31 算法族 ---------- */
typedef struct {
    bm_algo_q31_t ia;
    bm_algo_q31_t ib;
    bm_algo_q31_t ic;
} bm_algo_abc_q31_t;

typedef struct {
    bm_algo_q31_t i_alpha;
    bm_algo_q31_t i_beta;
} bm_algo_alphabeta_q31_t;

typedef struct {
    bm_algo_q31_t id;
    bm_algo_q31_t iq;
} bm_algo_dq_q31_t;

typedef struct {
    bm_algo_q31_t duty_a; /* 0 ~ BM_ALGO_Q31_ONE */
    bm_algo_q31_t duty_b;
    bm_algo_q31_t duty_c;
} bm_algo_svpwm_q31_out_t;

/**
 * @brief 执行 Q1.31 三相 Clarke 变换
 *
 * @param abc 三相输入；NULL 时静默返回
 * @param ab αβ 输出；NULL 时静默返回
 */
void bm_algo_clarke_q31(const bm_algo_abc_q31_t *abc, bm_algo_alphabeta_q31_t *ab);

/**
 * @brief 在 ia+ib+ic=0 假设下执行 Q1.31 两电阻 Clarke 变换
 *
 * @param ia A 相 Q1.31 电流
 * @param ib B 相 Q1.31 电流
 * @param ab αβ 输出；NULL 时静默返回
 */
void bm_algo_clarke_2shunt_q31(bm_algo_q31_t ia, bm_algo_q31_t ib, bm_algo_alphabeta_q31_t *ab);

/**
 * @brief 执行 Q1.31 Park 变换
 *
 * @param ab αβ 输入；NULL 时静默返回
 * @param sin_theta Q1.31 角度正弦
 * @param cos_theta Q1.31 角度余弦
 * @param dq dq 输出；NULL 时静默返回
 */
void bm_algo_park_q31(const bm_algo_alphabeta_q31_t *ab,
                      bm_algo_q31_t sin_theta,
                      bm_algo_q31_t cos_theta,
                      bm_algo_dq_q31_t *dq);

/**
 * @brief 执行 Q1.31 逆 Park 变换
 *
 * @param dq dq 输入；NULL 时静默返回
 * @param sin_theta Q1.31 角度正弦
 * @param cos_theta Q1.31 角度余弦
 * @param ab αβ 输出；NULL 时静默返回
 */
void bm_algo_inv_park_q31(const bm_algo_dq_q31_t *dq,
                          bm_algo_q31_t sin_theta,
                          bm_algo_q31_t cos_theta,
                          bm_algo_alphabeta_q31_t *ab);

/**
 * @brief 根据 Q1.31 αβ 电压计算三相 SVPWM 占空比
 *
 * @param v_alpha Q1.31 α 轴电压
 * @param v_beta Q1.31 β 轴电压
 * @param out 三相占空比输出；NULL 时静默返回
 */
void bm_algo_svpwm_q31(bm_algo_q31_t v_alpha,
                       bm_algo_q31_t v_beta,
                       bm_algo_svpwm_q31_out_t *out);

/* ---------- 通用定点扩展（第一批） ---------- */
typedef struct {
    bm_algo_q31_t min;
    bm_algo_q31_t max;
} bm_algo_integrator_q31_config_t;

typedef struct {
    bm_algo_q31_t integrator;
} bm_algo_integrator_q31_state_t;

/**
 * @brief 复位 Q1.31 积分器
 *
 * @param state 积分器状态；NULL 时静默返回
 * @param value 初始积分值
 */
void bm_algo_integrator_q31_reset(bm_algo_integrator_q31_state_t *state,
                                  bm_algo_q31_t value);

/**
 * @brief 执行一拍带上下限的 Q1.31 积分
 *
 * @param state 积分器状态
 * @param config 积分上下限
 * @param input 被积输入
 * @param dt_q31 Q1.31 时间步长，必须大于 0
 * @return 积分并限幅后的 Q1.31 值；参数无效时原样返回 input
 */
bm_algo_q31_t bm_algo_integrator_q31_step(bm_algo_integrator_q31_state_t *state,
                                          const bm_algo_integrator_q31_config_t *config,
                                          bm_algo_q31_t input,
                                          bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q31_t max_rise_per_s_q31;
    bm_algo_q31_t max_fall_per_s_q31;
} bm_algo_rate_limit_q31_config_t;

typedef struct {
    bm_algo_q31_t output;
} bm_algo_rate_limit_q31_state_t;

/**
 * @brief 复位 Q1.31 速率限制器
 *
 * @param state 限制器状态；NULL 时静默返回
 * @param output 初始输出
 */
void bm_algo_rate_limit_q31_reset(bm_algo_rate_limit_q31_state_t *state,
                                  bm_algo_q31_t output);

/**
 * @brief 按配置升降速率限制 Q1.31 目标
 *
 * @param state 限制器状态
 * @param config 每秒最大升降速率
 * @param target 目标值
 * @param dt_q31 Q1.31 时间步长，必须大于 0
 * @return 受限后的 Q1.31 输出；参数无效时原样返回 target
 */
bm_algo_q31_t bm_algo_rate_limit_q31_step(bm_algo_rate_limit_q31_state_t *state,
                                          const bm_algo_rate_limit_q31_config_t *config,
                                          bm_algo_q31_t target,
                                          bm_algo_q31_t dt_q31);

/**
 * @brief 对 Q1.31 输入应用连续死区补偿
 *
 * @param value 输入值
 * @param width_q31 死区半宽；非正时禁用死区
 * @return 死区内返回 0；死区外扣除相应符号的 width_q31 后返回
 */
bm_algo_q31_t bm_algo_deadband_q31(bm_algo_q31_t value, bm_algo_q31_t width_q31);

typedef struct {
    bm_algo_q15_t b0;
    bm_algo_q15_t b1;
    bm_algo_q15_t b2;
    bm_algo_q15_t a1;
    bm_algo_q15_t a2;
    bm_algo_q15_t out_min;
    bm_algo_q15_t out_max;
} bm_algo_pr_q15_config_t;

typedef struct {
    bm_algo_q15_t x1;
    bm_algo_q15_t x2;
    bm_algo_q15_t y1;
    bm_algo_q15_t y2;
    bm_algo_q15_t output;
} bm_algo_pr_q15_state_t;

/**
 * @brief 清零 Q1.15 PR 控制器历史状态
 *
 * @param state 控制器状态；NULL 时静默返回
 */
void bm_algo_pr_q15_reset(bm_algo_pr_q15_state_t *state);

/**
 * @brief 执行一拍 Q1.15 PR 差分控制并限幅
 *
 * @param state 控制器状态
 * @param config PR 差分系数及输出限幅
 * @param error 本拍误差
 * @return Q1.15 控制输出；state 或 config 为 NULL 时返回 0
 */
bm_algo_q15_t bm_algo_pr_q15_step(bm_algo_pr_q15_state_t *state,
                                  const bm_algo_pr_q15_config_t *config,
                                  bm_algo_q15_t error);

/* ---------- 通用定点扩展（第二批：轨迹） ---------- */
typedef struct {
    bm_algo_q31_t rate_per_s_q31;
} bm_algo_ramp_q31_config_t;

typedef struct {
    bm_algo_q31_t output;
    int done;
} bm_algo_ramp_q31_state_t;

/**
 * @brief 复位 Q1.31 斜坡发生器
 *
 * @param state 斜坡状态；NULL 时静默返回
 * @param output 初始输出
 */
void bm_algo_ramp_q31_reset(bm_algo_ramp_q31_state_t *state, bm_algo_q31_t output);

/**
 * @brief 以配置速率向 Q1.31 目标推进一拍
 *
 * @param state 斜坡状态
 * @param config Q1.31 每秒速率
 * @param target 目标值
 * @param dt_q31 Q1.31 时间步长，必须大于 0
 * @return 本拍 Q1.31 输出；参数无效时原样返回 target
 */
bm_algo_q31_t bm_algo_ramp_q31_step(bm_algo_ramp_q31_state_t *state,
                                    const bm_algo_ramp_q31_config_t *config,
                                    bm_algo_q31_t target,
                                    bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q31_t max_vel_q31;
    bm_algo_q31_t max_accel_q31;
    bm_algo_q31_t max_jerk_q31;
} bm_algo_scurve_q31_config_t;

typedef struct {
    bm_algo_q31_t position;
    bm_algo_q31_t velocity;
    bm_algo_q31_t acceleration;
    bm_algo_q31_t target;
    int done;
} bm_algo_scurve_q31_state_t;

/**
 * @brief 复位 Q1.31 S 曲线轨迹状态
 *
 * @param state 轨迹状态；NULL 时静默返回
 * @param position 初始位置
 * @param velocity 初始速度
 * @param acceleration 初始加速度
 */
void bm_algo_scurve_q31_reset(bm_algo_scurve_q31_state_t *state,
                              bm_algo_q31_t position,
                              bm_algo_q31_t velocity,
                              bm_algo_q31_t acceleration);

/**
 * @brief 设置 Q1.31 S 曲线目标位置
 *
 * @param state 轨迹状态；NULL 时静默返回
 * @param target 目标位置
 */
void bm_algo_scurve_q31_set_target(bm_algo_scurve_q31_state_t *state,
                                   bm_algo_q31_t target);

/**
 * @brief 通过浮点轨迹核推进一拍 Q1.31 S 曲线
 *
 * @param state 轨迹状态
 * @param config 最大速度、加速度和加加速度
 * @param dt_q31 Q1.31 时间步长，必须大于 0
 * @return 量化并饱和后的 Q1.31 位置；参数无效时返回当前位置，state 为 NULL 时返回 0
 */
bm_algo_q31_t bm_algo_scurve_q31_step(bm_algo_scurve_q31_state_t *state,
                                      const bm_algo_scurve_q31_config_t *config,
                                      bm_algo_q31_t dt_q31);

/* ---------- 通用定点扩展（第三批） ---------- */
#define BM_ALGO_MOVING_AVG_Q15_MAX  16u

typedef struct {
    uint16_t window_size;
} bm_algo_moving_avg_q15_config_t;

typedef struct {
    bm_algo_q15_t samples[BM_ALGO_MOVING_AVG_Q15_MAX];
    uint16_t      count;
    uint16_t      index;
} bm_algo_moving_avg_q15_state_t;

/**
 * @brief 清零 Q1.15 移动平均状态
 *
 * @param state 滤波状态；NULL 时静默返回
 */
void bm_algo_moving_avg_q15_reset(bm_algo_moving_avg_q15_state_t *state);

/**
 * @brief 更新 Q1.15 有限窗口移动平均
 *
 * @param state 滤波状态
 * @param config 窗口长度，超过 16 时按 16 处理
 * @param input 本拍输入
 * @return 饱和后的 Q1.15 平均值；参数或窗口无效时原样返回 input
 */
bm_algo_q15_t bm_algo_moving_avg_q15_step(bm_algo_moving_avg_q15_state_t *state,
                                          const bm_algo_moving_avg_q15_config_t *config,
                                          bm_algo_q15_t input);

typedef struct {
    bm_algo_q15_t kp;
    bm_algo_q15_t ki;
    bm_algo_q15_t kd;
    bm_algo_q15_t out_min;
    bm_algo_q15_t out_max;
    bm_algo_q15_t integrator_min;
    bm_algo_q15_t integrator_max;
    bm_algo_q15_t d_filter_alpha_q15;
} bm_algo_pid_q15_config_t;

typedef struct {
    bm_algo_q15_t integrator;
    bm_algo_q15_t prev_error;
    bm_algo_q15_t d_filtered;
    bm_algo_q15_t output;
} bm_algo_pid_q15_state_t;

/**
 * @brief 复位 Q1.15 PID 控制器状态
 *
 * @param state 状态；NULL 时静默返回
 * @param output 初始输出
 */
void bm_algo_pid_q15_reset(bm_algo_pid_q15_state_t *state, bm_algo_q15_t output);

/**
 * @brief 执行一拍带微分滤波和限幅的 Q1.15 PID 控制
 *
 * @param state 控制器状态
 * @param config PID 参数及限幅
 * @param error 本拍误差
 * @param dt_q15 Q1.15 时间步长，必须大于 0
 * @return 限幅后的 Q1.15 输出；参数无效或时间步长非正时返回 0
 */
bm_algo_q15_t bm_algo_pid_q15_step(bm_algo_pid_q15_state_t *state,
                                   const bm_algo_pid_q15_config_t *config,
                                   bm_algo_q15_t error,
                                   bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q31_t low_threshold;
    bm_algo_q31_t high_threshold;
} bm_algo_hysteresis_q31_config_t;

typedef struct {
    int output_on;
} bm_algo_hysteresis_q31_state_t;

/**
 * @brief 复位 Q1.31 迟滞比较器为关闭状态
 *
 * @param state 比较器状态；NULL 时静默返回
 */
void bm_algo_hysteresis_q31_reset(bm_algo_hysteresis_q31_state_t *state);

/**
 * @brief 执行 Q1.31 双阈值迟滞比较
 *
 * @param state 比较器状态
 * @param config 低阈值和高阈值
 * @param input 输入值
 * @return 开启时返回 BM_ALGO_Q31_ONE，否则返回 0；参数无效时返回 0
 */
bm_algo_q31_t bm_algo_hysteresis_q31_step(bm_algo_hysteresis_q31_state_t *state,
                                          const bm_algo_hysteresis_q31_config_t *config,
                                          bm_algo_q31_t input);

/* ---------- 通用定点扩展（第四批：轨迹） ---------- */
typedef struct {
    bm_algo_q31_t max_vel_q31;
    bm_algo_q31_t max_accel_q31;
    bm_algo_q31_t max_decel_q31;
} bm_algo_trapezoid_q31_config_t;

typedef struct {
    bm_algo_q31_t position;
    bm_algo_q31_t velocity;
    bm_algo_q31_t target;
    int done;
} bm_algo_trapezoid_q31_state_t;

/**
 * @brief 复位 Q1.31 梯形轨迹状态
 *
 * @param state 轨迹状态；NULL 时静默返回
 * @param position 初始位置
 * @param velocity 初始速度
 */
void bm_algo_trapezoid_q31_reset(bm_algo_trapezoid_q31_state_t *state,
                                 bm_algo_q31_t position,
                                 bm_algo_q31_t velocity);

/**
 * @brief 设置 Q1.31 梯形轨迹目标位置
 *
 * @param state 轨迹状态；NULL 时静默返回
 * @param target 目标位置
 */
void bm_algo_trapezoid_q31_set_target(bm_algo_trapezoid_q31_state_t *state,
                                      bm_algo_q31_t target);

/**
 * @brief 推进一拍 Q1.31 梯形速度轨迹
 *
 * @param state 轨迹状态
 * @param config 最大速度、加速度和减速度
 * @param dt_q31 Q1.31 时间步长，必须大于 0
 * @return 本拍位置；state/config 无效或时间步长非正时返回 0，约束无效时保持当前位置
 */
bm_algo_q31_t bm_algo_trapezoid_q31_step(bm_algo_trapezoid_q31_state_t *state,
                                         const bm_algo_trapezoid_q31_config_t *config,
                                         bm_algo_q31_t dt_q31);

/* ---------- 通用定点扩展（第五批：滤波） ---------- */
typedef struct {
    bm_algo_q31_t alpha_q31;
} bm_algo_lpf1_q31_config_t;

typedef struct {
    bm_algo_q31_t output;
} bm_algo_lpf1_q31_state_t;

/**
 * @brief 复位 Q1.31 一阶低通滤波器
 *
 * @param state 滤波状态；NULL 时静默返回
 * @param output 初始输出
 */
void bm_algo_lpf1_q31_reset(bm_algo_lpf1_q31_state_t *state, bm_algo_q31_t output);

/**
 * @brief 执行一拍 Q1.31 一阶低通滤波
 *
 * @param state 滤波状态
 * @param config 含 Q1.31 平滑系数的配置
 * @param input 本拍输入
 * @return Q1.31 滤波输出；state 或 config 为 NULL 时原样返回 input
 */
bm_algo_q31_t bm_algo_lpf1_q31_step(bm_algo_lpf1_q31_state_t *state,
                                    const bm_algo_lpf1_q31_config_t *config,
                                    bm_algo_q31_t input);

typedef struct {
    bm_algo_q15_t samples[3];
    uint8_t       count;
} bm_algo_median3_q15_state_t;

/**
 * @brief 清零 Q1.15 三点中值滤波状态
 *
 * @param state 滤波状态；NULL 时静默返回
 */
void bm_algo_median3_q15_reset(bm_algo_median3_q15_state_t *state);

/**
 * @brief 更新 Q1.15 三点中值滤波器
 *
 * @param state 滤波状态
 * @param input 本拍输入
 * @return 预热阶段的有界输出或三点中值；state 为 NULL 时原样返回 input
 */
bm_algo_q15_t bm_algo_median3_q15_step(bm_algo_median3_q15_state_t *state,
                                       bm_algo_q15_t input);

typedef struct {
    bm_algo_q15_t alpha_q15;
} bm_algo_hpf1_q15_config_t;

typedef struct {
    bm_algo_q15_t prev_input;
    bm_algo_q15_t prev_output;
} bm_algo_hpf1_q15_state_t;

/**
 * @brief 清零 Q1.15 一阶高通滤波状态
 *
 * @param state 滤波状态；NULL 时静默返回
 */
void bm_algo_hpf1_q15_reset(bm_algo_hpf1_q15_state_t *state);

/**
 * @brief 执行一拍 Q1.15 一阶高通滤波
 *
 * @param state 滤波状态
 * @param config Q1.15 高通系数
 * @param input 本拍输入
 * @return Q1.15 高通输出；state 或 config 为 NULL 时原样返回 input
 */
bm_algo_q15_t bm_algo_hpf1_q15_step(bm_algo_hpf1_q15_state_t *state,
                                    const bm_algo_hpf1_q15_config_t *config,
                                    bm_algo_q15_t input);

/* ---------- 通用定点扩展（第六批） ---------- */
typedef struct {
    bm_algo_q31_t coeff_q31;
} bm_algo_differentiator_q31_config_t;

typedef struct {
    bm_algo_q31_t prev_input;
    bm_algo_q31_t derivative;
} bm_algo_differentiator_q31_state_t;

/**
 * @brief 清零 Q1.31 微分器状态
 *
 * @param state 微分器状态；NULL 时静默返回
 */
void bm_algo_differentiator_q31_reset(bm_algo_differentiator_q31_state_t *state);

/**
 * @brief 计算 Q1.31 输入的一拍带系数差分
 *
 * @param state 微分器状态
 * @param config Q1.31 微分系数
 * @param input 本拍输入
 * @param dt_q31 Q1.31 时间步长，必须大于 0
 * @return 饱和后的 Q1.31 导数；参数无效时返回 0
 */
bm_algo_q31_t bm_algo_differentiator_q31_step(
    bm_algo_differentiator_q31_state_t *state,
    const bm_algo_differentiator_q31_config_t *config,
    bm_algo_q31_t input,
    bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q15_t alpha_q15;
} bm_algo_envelope_q15_config_t;

typedef struct {
    bm_algo_q15_t envelope;
} bm_algo_envelope_q15_state_t;

/**
 * @brief 复位 Q1.15 包络跟踪器
 *
 * @param state 跟踪器状态；NULL 时静默返回
 * @param output 初始包络
 */
void bm_algo_envelope_q15_reset(bm_algo_envelope_q15_state_t *state,
                                bm_algo_q15_t output);

/**
 * @brief 对 Q1.15 输入绝对值执行一阶包络跟踪
 *
 * @param state 跟踪器状态
 * @param config Q1.15 平滑系数
 * @param input 本拍输入
 * @return Q1.15 包络；state 或 config 为 NULL 时原样返回 input
 */
bm_algo_q15_t bm_algo_envelope_q15_step(bm_algo_envelope_q15_state_t *state,
                                        const bm_algo_envelope_q15_config_t *config,
                                        bm_algo_q15_t input);

#define BM_ALGO_RMS_Q15_MAX  16u

typedef struct {
    uint16_t window_size;
} bm_algo_rms_q15_config_t;

typedef struct {
    bm_algo_q15_t samples[BM_ALGO_RMS_Q15_MAX];
    uint16_t      count;
    uint16_t      index;
} bm_algo_rms_q15_state_t;

/**
 * @brief 清零 Q1.15 RMS 窗口状态
 *
 * @param state RMS 状态；NULL 时静默返回
 */
void bm_algo_rms_q15_reset(bm_algo_rms_q15_state_t *state);

/**
 * @brief 更新有限窗口 Q1.15 均方根
 *
 * @param state RMS 状态
 * @param config 窗口长度，超过 16 时按 16 处理
 * @param input 本拍输入
 * @return Q1.15 RMS 值；参数或窗口无效时原样返回 input，结果按 Q15 范围饱和
 */
bm_algo_q15_t bm_algo_rms_q15_step(bm_algo_rms_q15_state_t *state,
                                   const bm_algo_rms_q15_config_t *config,
                                   bm_algo_q15_t input);

/* ---------- 通用定点扩展（第七批） ---------- */
typedef struct {
    bm_algo_q31_t nominal_capacity_q31;
    bm_algo_q31_t coulomb_efficiency_q31;
    bm_algo_q31_t soc_min;
    bm_algo_q31_t soc_max;
} bm_algo_coulomb_q31_config_t;

typedef struct {
    bm_algo_q31_t soc;
} bm_algo_coulomb_q31_state_t;

/**
 * @brief 复位 Q1.31 库仑 SOC 状态
 *
 * @param state SOC 状态；NULL 时静默返回
 * @param soc_init 初始 SOC
 */
void bm_algo_coulomb_q31_reset(bm_algo_coulomb_q31_state_t *state,
                               bm_algo_q31_t soc_init);

/**
 * @brief 按 C-rate 与小时比例积分 Q1.31 SOC
 *
 * @param state SOC 状态
 * @param config 容量、效率及 SOC 限幅
 * @param current_q31 Q1.31 充放电倍率
 * @param dt_q31 Q1.31 小时比例，必须大于 0
 * @return 限幅后的 Q1.31 SOC；配置无效时保持现有 SOC，state 为 NULL 时返回 0
 */
bm_algo_q31_t bm_algo_coulomb_q31_step(bm_algo_coulomb_q31_state_t *state,
                                       const bm_algo_coulomb_q31_config_t *config,
                                       bm_algo_q31_t current_q31,
                                       bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q31_t alpha_q31;
} bm_algo_hpf1_q31_config_t;

typedef struct {
    bm_algo_q31_t prev_input;
    bm_algo_q31_t prev_output;
} bm_algo_hpf1_q31_state_t;

/**
 * @brief 清零 Q1.31 一阶高通滤波状态
 *
 * @param state 滤波状态；NULL 时静默返回
 */
void bm_algo_hpf1_q31_reset(bm_algo_hpf1_q31_state_t *state);

/**
 * @brief 执行一拍 Q1.31 一阶高通滤波
 *
 * @param state 滤波状态
 * @param config Q1.31 高通系数
 * @param input 本拍输入
 * @return Q1.31 高通输出；state 或 config 为 NULL 时原样返回 input
 */
bm_algo_q31_t bm_algo_hpf1_q31_step(bm_algo_hpf1_q31_state_t *state,
                                    const bm_algo_hpf1_q31_config_t *config,
                                    bm_algo_q31_t input);

#define BM_ALGO_MOVING_AVG_Q31_MAX  16u

typedef struct {
    uint16_t window_size;
} bm_algo_moving_avg_q31_config_t;

typedef struct {
    bm_algo_q31_t samples[BM_ALGO_MOVING_AVG_Q31_MAX];
    uint16_t      count;
    uint16_t      index;
} bm_algo_moving_avg_q31_state_t;

/**
 * @brief 清零 Q1.31 移动平均状态
 *
 * @param state 滤波状态；NULL 时静默返回
 */
void bm_algo_moving_avg_q31_reset(bm_algo_moving_avg_q31_state_t *state);

/**
 * @brief 更新 Q1.31 有限窗口移动平均
 *
 * @param state 滤波状态
 * @param config 窗口长度，超过 16 时按 16 处理
 * @param input 本拍输入
 * @return 饱和后的 Q1.31 平均值；参数或窗口无效时原样返回 input
 */
bm_algo_q31_t bm_algo_moving_avg_q31_step(bm_algo_moving_avg_q31_state_t *state,
                                          const bm_algo_moving_avg_q31_config_t *config,
                                          bm_algo_q31_t input);

/**
 * @brief 对 Q1.15 输入应用连续死区补偿
 *
 * @param value 输入值
 * @param width_q15 死区半宽；非正时禁用死区
 * @return 死区内返回 0；死区外扣除相应符号的 width_q15 后返回
 */
bm_algo_q15_t bm_algo_deadband_q15(bm_algo_q15_t value, bm_algo_q15_t width_q15);

typedef struct {
    bm_algo_q15_t low_threshold;
    bm_algo_q15_t high_threshold;
} bm_algo_hysteresis_q15_config_t;

typedef struct {
    int output_on;
} bm_algo_hysteresis_q15_state_t;

/**
 * @brief 复位 Q1.15 迟滞比较器为关闭状态
 *
 * @param state 比较器状态；NULL 时静默返回
 */
void bm_algo_hysteresis_q15_reset(bm_algo_hysteresis_q15_state_t *state);

/**
 * @brief 执行 Q1.15 双阈值迟滞比较
 *
 * @param state 比较器状态
 * @param config 低阈值和高阈值
 * @param input 输入值
 * @return 开启时返回 BM_ALGO_Q15_ONE，否则返回 0；参数无效时返回 0
 */
bm_algo_q15_t bm_algo_hysteresis_q15_step(bm_algo_hysteresis_q15_state_t *state,
                                          const bm_algo_hysteresis_q15_config_t *config,
                                          bm_algo_q15_t input);

/* ---------- 通用定点扩展（第八批） ---------- */
typedef struct {
    bm_algo_q15_t min;
    bm_algo_q15_t max;
} bm_algo_integrator_q15_config_t;

typedef struct {
    bm_algo_q15_t integrator;
} bm_algo_integrator_q15_state_t;

/**
 * @brief 复位 Q1.15 积分器
 *
 * @param state 积分器状态；NULL 时静默返回
 * @param value 初始积分值
 */
void bm_algo_integrator_q15_reset(bm_algo_integrator_q15_state_t *state,
                                  bm_algo_q15_t value);

/**
 * @brief 执行一拍带上下限的 Q1.15 积分
 *
 * @param state 积分器状态
 * @param config 积分上下限
 * @param input 被积输入
 * @param dt_q15 Q1.15 时间步长，必须大于 0
 * @return 积分并限幅后的 Q1.15 值；参数无效时原样返回 input
 */
bm_algo_q15_t bm_algo_integrator_q15_step(bm_algo_integrator_q15_state_t *state,
                                          const bm_algo_integrator_q15_config_t *config,
                                          bm_algo_q15_t input,
                                          bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q15_t max_rise_per_s_q15;
    bm_algo_q15_t max_fall_per_s_q15;
} bm_algo_rate_limit_q15_config_t;

typedef struct {
    bm_algo_q15_t output;
} bm_algo_rate_limit_q15_state_t;

/**
 * @brief 复位 Q1.15 速率限制器
 *
 * @param state 限制器状态；NULL 时静默返回
 * @param output 初始输出
 */
void bm_algo_rate_limit_q15_reset(bm_algo_rate_limit_q15_state_t *state,
                                  bm_algo_q15_t output);

/**
 * @brief 按配置升降速率限制 Q1.15 目标
 *
 * @param state 限制器状态
 * @param config 每秒最大升降速率
 * @param target 目标值
 * @param dt_q15 Q1.15 时间步长，必须大于 0
 * @return 受限后的 Q1.15 输出；参数无效时原样返回 target
 */
bm_algo_q15_t bm_algo_rate_limit_q15_step(bm_algo_rate_limit_q15_state_t *state,
                                          const bm_algo_rate_limit_q15_config_t *config,
                                          bm_algo_q15_t target,
                                          bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q15_t b0;
    bm_algo_q15_t b1;
    bm_algo_q15_t a1;
} bm_algo_lead_lag_q15_config_t;

typedef struct {
    bm_algo_q15_t x1;
    bm_algo_q15_t y1;
} bm_algo_lead_lag_q15_state_t;

/**
 * @brief 初始化并清零 Q1.15 超前滞后滤波状态
 *
 * @param state 状态
 * @param config Q1.15 系数配置
 * @return BM_OK 成功；state 或 config 为 NULL 时返回 BM_ERR_INVALID
 */
int bm_algo_lead_lag_q15_init(bm_algo_lead_lag_q15_state_t *state,
                              const bm_algo_lead_lag_q15_config_t *config);

/**
 * @brief 清零 Q1.15 超前滞后滤波状态
 *
 * @param state 状态；NULL 时静默返回
 */
void bm_algo_lead_lag_q15_reset(bm_algo_lead_lag_q15_state_t *state);

/**
 * @brief 执行一拍 Q1.15 超前滞后滤波
 *
 * @param state 滤波状态
 * @param config Q1.15 系数
 * @param input 本拍输入
 * @return 饱和后的 Q1.15 输出；state 或 config 为 NULL 时原样返回 input
 */
bm_algo_q15_t bm_algo_lead_lag_q15_step(bm_algo_lead_lag_q15_state_t *state,
                                        const bm_algo_lead_lag_q15_config_t *config,
                                        bm_algo_q15_t input);

typedef struct {
    bm_algo_q31_t b0;
    bm_algo_q31_t b1;
    bm_algo_q31_t b2;
    bm_algo_q31_t a1;
    bm_algo_q31_t a2;
} bm_algo_biquad_q31_config_t;

typedef struct {
    bm_algo_q31_t z1;
    bm_algo_q31_t z2;
} bm_algo_biquad_q31_state_t;

/**
 * @brief 清零 Q1.31 双二阶滤波器延迟状态
 *
 * @param state 滤波状态；NULL 时静默返回
 */
void bm_algo_biquad_q31_reset(bm_algo_biquad_q31_state_t *state);

/**
 * @brief 执行一拍 Q1.31 双二阶滤波并饱和输出
 *
 * @param state 滤波状态
 * @param config Q1.31 滤波系数
 * @param input 本拍输入
 * @return Q1.31 滤波输出；state 或 config 为 NULL 时原样返回 input
 */
bm_algo_q31_t bm_algo_biquad_q31_step(bm_algo_biquad_q31_state_t *state,
                                      const bm_algo_biquad_q31_config_t *config,
                                      bm_algo_q31_t input);

/* ---------- 通用定点扩展（第九批） ---------- */
typedef struct {
    bm_algo_q15_t plant_gain_q15;
    bm_algo_q15_t lpf_alpha_q15;
} bm_algo_dob_q15_config_t;

typedef struct {
    bm_algo_q15_t y_hat;
    bm_algo_q15_t disturbance;
} bm_algo_dob_q15_state_t;

/**
 * @brief 清零 Q1.15 扰动观测器状态
 *
 * @param state 观测器状态；NULL 时静默返回
 */
void bm_algo_dob_q15_reset(bm_algo_dob_q15_state_t *state);

/**
 * @brief DOB 单步（Q15）：由 u/y 估计扰动
 *
 * @param state 观测器状态（不可为 NULL）
 * @param config 模型增益与低通系数（不可为 NULL）
 * @param u_q15 控制输入
 * @param y_q15 被控输出测量
 * @param disturbance_out 可选扰动估计输出（可为 NULL）
 * @return 扰动估计（Q15）
 */
bm_algo_q15_t bm_algo_dob_q15_step(bm_algo_dob_q15_state_t *state,
                                   const bm_algo_dob_q15_config_t *config,
                                   bm_algo_q15_t u_q15,
                                   bm_algo_q15_t y_q15,
                                   bm_algo_q15_t *disturbance_out);

typedef struct {
    bm_algo_q31_t alpha_q31;
} bm_algo_envelope_q31_config_t;

typedef struct {
    bm_algo_q31_t envelope;
} bm_algo_envelope_q31_state_t;

/**
 * @brief 复位 Q1.31 包络跟踪器
 *
 * @param state 跟踪器状态；NULL 时静默返回
 * @param output 初始包络
 */
void bm_algo_envelope_q31_reset(bm_algo_envelope_q31_state_t *state,
                                bm_algo_q31_t output);

/**
 * @brief 对 Q1.31 输入绝对值执行一阶包络跟踪
 *
 * @param state 跟踪器状态
 * @param config Q1.31 平滑系数
 * @param input 本拍输入
 * @return Q1.31 包络；state 或 config 为 NULL 时原样返回 input
 */
bm_algo_q31_t bm_algo_envelope_q31_step(bm_algo_envelope_q31_state_t *state,
                                        const bm_algo_envelope_q31_config_t *config,
                                        bm_algo_q31_t input);

#define BM_ALGO_RMS_Q31_MAX  16u

typedef struct {
    uint16_t window_size;
} bm_algo_rms_q31_config_t;

typedef struct {
    bm_algo_q31_t samples[BM_ALGO_RMS_Q31_MAX];
    uint16_t      count;
    uint16_t      index;
} bm_algo_rms_q31_state_t;

/**
 * @brief 清零 Q1.31 RMS 窗口状态
 *
 * @param state RMS 状态；NULL 时静默返回
 */
void bm_algo_rms_q31_reset(bm_algo_rms_q31_state_t *state);

/**
 * @brief 更新有限窗口 Q1.31 均方根
 *
 * @param state RMS 状态
 * @param config 窗口长度，超过 16 时按 16 处理
 * @param input 本拍输入
 * @return Q1.31 RMS 值；参数或窗口无效时原样返回 input，溢出时返回 INT32_MAX
 */
bm_algo_q31_t bm_algo_rms_q31_step(bm_algo_rms_q31_state_t *state,
                                   const bm_algo_rms_q31_config_t *config,
                                   bm_algo_q31_t input);

typedef struct {
    int last_direction;         /**< 上次有效运动方向（1/-1/0） */
    bm_algo_q31_t offset_fwd;   /**< 正向累计补偿量，范围 [0, width] */
    bm_algo_q31_t offset_rev;   /**< 反向累计补偿量，范围 [0, width] */
} bm_algo_backlash_q31_state_t;

/**
 * @brief 清零 Q1.31 双向背隙补偿状态
 *
 * @param state 补偿状态；NULL 时静默返回
 */
void bm_algo_backlash_q31_reset(bm_algo_backlash_q31_state_t *state);

/**
 * @brief 背隙逆补偿（Q31）：双向独立偏移，换向时切换对应偏移继续渐进
 *
 * 定点移植自 float 版 v1.3（bm_algo_backlash_inverse）：正向用 offset_fwd、
 * 反向用 offset_rev，各自向 width 渐进（每步最多 slope），换向不清零。
 *
 * @param command_q31 原始指令
 * @param state 背隙状态（不可为 NULL）
 * @param width_q31 总背隙宽度
 * @param slope_q31 每步最大补偿量（>0）
 * @return 补偿后指令
 */
bm_algo_q31_t bm_algo_backlash_inverse_q31(bm_algo_q31_t command_q31,
                                           bm_algo_backlash_q31_state_t *state,
                                           bm_algo_q31_t width_q31,
                                           bm_algo_q31_t slope_q31);

/* ---------- 通用定点扩展（第十批） ---------- */
typedef struct {
    bm_algo_q15_t coeff_q15;
} bm_algo_differentiator_q15_config_t;

typedef struct {
    bm_algo_q15_t prev_input;
    bm_algo_q15_t derivative;
} bm_algo_differentiator_q15_state_t;

/**
 * @brief 清零 Q1.15 微分器状态
 *
 * @param state 微分器状态；NULL 时静默返回
 */
void bm_algo_differentiator_q15_reset(bm_algo_differentiator_q15_state_t *state);

/**
 * @brief 计算 Q1.15 输入的一拍带系数差分
 *
 * @param state 微分器状态
 * @param config Q1.15 微分系数
 * @param input 本拍输入
 * @param dt_q15 Q1.15 时间步长，必须大于 0
 * @return 饱和后的 Q1.15 导数；参数无效时返回 0
 */
bm_algo_q15_t bm_algo_differentiator_q15_step(
    bm_algo_differentiator_q15_state_t *state,
    const bm_algo_differentiator_q15_config_t *config,
    bm_algo_q15_t input,
    bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q31_t plant_gain_q31;
    bm_algo_q31_t lpf_alpha_q31;
} bm_algo_dob_q31_config_t;

typedef struct {
    bm_algo_q31_t y_hat;
    bm_algo_q31_t disturbance;
} bm_algo_dob_q31_state_t;

/**
 * @brief 清零 Q1.31 扰动观测器状态
 *
 * @param state 观测器状态；NULL 时静默返回
 */
void bm_algo_dob_q31_reset(bm_algo_dob_q31_state_t *state);

/**
 * @brief DOB 单步（Q31）：由 u/y 估计扰动
 *
 * @param state 观测器状态（不可为 NULL）
 * @param config 模型增益与低通系数（不可为 NULL）
 * @param u_q31 控制输入
 * @param y_q31 被控输出测量
 * @param disturbance_out 可选扰动估计输出（可为 NULL）
 * @return 扰动估计（Q31）
 */
bm_algo_q31_t bm_algo_dob_q31_step(bm_algo_dob_q31_state_t *state,
                                   const bm_algo_dob_q31_config_t *config,
                                   bm_algo_q31_t u_q31,
                                   bm_algo_q31_t y_q31,
                                   bm_algo_q31_t *disturbance_out);

typedef struct {
    bm_algo_q15_t nominal_capacity_q15;
    bm_algo_q15_t coulomb_efficiency_q15;
    bm_algo_q15_t soc_min;
    bm_algo_q15_t soc_max;
} bm_algo_coulomb_q15_config_t;

typedef struct {
    bm_algo_q15_t soc;
} bm_algo_coulomb_q15_state_t;

/**
 * @brief 复位 Q1.15 库仑 SOC 状态
 *
 * @param state SOC 状态；NULL 时静默返回
 * @param soc_init 初始 SOC
 */
void bm_algo_coulomb_q15_reset(bm_algo_coulomb_q15_state_t *state,
                               bm_algo_q15_t soc_init);

/**
 * @brief 按 C-rate 与小时比例积分 Q1.15 SOC
 *
 * @param state SOC 状态
 * @param config 容量、效率及 SOC 限幅
 * @param current_q15 Q1.15 充放电倍率
 * @param dt_q15 Q1.15 小时比例，必须大于 0
 * @return 限幅后的 Q1.15 SOC；配置无效时保持现有 SOC，state 为 NULL 时返回 0
 */
bm_algo_q15_t bm_algo_coulomb_q15_step(bm_algo_coulomb_q15_state_t *state,
                                       const bm_algo_coulomb_q15_config_t *config,
                                       bm_algo_q15_t current_q15,
                                       bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q31_t b0;
    bm_algo_q31_t b1;
    bm_algo_q31_t a1;
} bm_algo_lead_lag_q31_config_t;

typedef struct {
    bm_algo_q31_t x1;
    bm_algo_q31_t y1;
} bm_algo_lead_lag_q31_state_t;

/**
 * @brief 初始化并清零 Q1.31 超前滞后滤波状态
 *
 * @param state 状态
 * @param config Q1.31 系数配置
 * @return BM_OK 成功；state 或 config 为 NULL 时返回 BM_ERR_INVALID
 */
int bm_algo_lead_lag_q31_init(bm_algo_lead_lag_q31_state_t *state,
                              const bm_algo_lead_lag_q31_config_t *config);

/**
 * @brief 清零 Q1.31 超前滞后滤波状态
 *
 * @param state 状态；NULL 时静默返回
 */
void bm_algo_lead_lag_q31_reset(bm_algo_lead_lag_q31_state_t *state);

/**
 * @brief 执行一拍 Q1.31 超前滞后滤波
 *
 * @param state 滤波状态
 * @param config Q1.31 系数
 * @param input 本拍输入
 * @return 饱和后的 Q1.31 输出；state 或 config 为 NULL 时原样返回 input
 */
bm_algo_q31_t bm_algo_lead_lag_q31_step(bm_algo_lead_lag_q31_state_t *state,
                                        const bm_algo_lead_lag_q31_config_t *config,
                                        bm_algo_q31_t input);

typedef struct {
    bm_algo_q15_t alpha_q15;
} bm_algo_complementary_q15_config_t;

typedef struct {
    bm_algo_q15_t roll_rad;
    bm_algo_q15_t pitch_rad;
} bm_algo_complementary_q15_state_t;

/**
 * @brief 清零 Q1.15 互补姿态滤波状态
 *
 * @param state 姿态状态；NULL 时静默返回
 */
void bm_algo_complementary_q15_reset(bm_algo_complementary_q15_state_t *state);

/**
 * @brief 通过浮点 atan2 桥接更新 Q1.15 横滚角和俯仰角
 *
 * @param state 姿态状态；NULL 时静默返回
 * @param config 含 Q1.15 融合系数的配置；NULL 时静默返回
 * @param gx_q15 X 轴 Q1.15 角速度
 * @param gy_q15 Y 轴 Q1.15 角速度
 * @param gz_q15 Z 轴 Q1.15 角速度，当前实现忽略
 * @param ax_q15 X 轴 Q1.15 加速度
 * @param ay_q15 Y 轴 Q1.15 加速度
 * @param az_q15 Z 轴 Q1.15 加速度
 * @param dt_q15 Q1.15 时间步长；非正时静默返回
 */
void bm_algo_complementary_q15_step(bm_algo_complementary_q15_state_t *state,
                                    const bm_algo_complementary_q15_config_t *config,
                                    bm_algo_q15_t gx_q15,
                                    bm_algo_q15_t gy_q15,
                                    bm_algo_q15_t gz_q15,
                                    bm_algo_q15_t ax_q15,
                                    bm_algo_q15_t ay_q15,
                                    bm_algo_q15_t az_q15,
                                    bm_algo_q15_t dt_q15);

/**
 * @brief 计算无状态 Q1.31 前馈输出
 *
 * @param reference_q31 Q1.31 参考量
 * @param gain_q31 Q1.31 增益
 * @param bias_q31 Q1.31 偏置
 * @return reference×gain+bias 的饱和 Q1.31 结果
 */
bm_algo_q31_t bm_algo_feedforward_q31_step(bm_algo_q31_t reference_q31,
                                         bm_algo_q31_t gain_q31,
                                         bm_algo_q31_t bias_q31);

/**
 * @brief 计算无状态 Q1.15 前馈输出
 *
 * @param reference_q15 Q1.15 参考量
 * @param gain_q15 Q1.15 增益
 * @param bias_q15 Q1.15 偏置
 * @return reference×gain+bias 的饱和 Q1.15 结果
 */
bm_algo_q15_t bm_algo_feedforward_q15_step(bm_algo_q15_t reference_q15,
                                         bm_algo_q15_t gain_q15,
                                         bm_algo_q15_t bias_q15);

/* ---------- 通用定点扩展（第十一批：控制/信号质量） ---------- */
typedef struct {
    bm_algo_q15_t kp;
    bm_algo_q15_t ki;
    bm_algo_q15_t out_min;
    bm_algo_q15_t out_max;
    bm_algo_q15_t integrator_min;
    bm_algo_q15_t integrator_max;
} bm_algo_pi_q15_config_t;

typedef struct {
    bm_algo_q15_t integrator;
    bm_algo_q15_t output;
} bm_algo_pi_q15_state_t;

/**
 * @brief 复位 Q1.15 PI 控制器状态
 *
 * @param state 状态；NULL 时静默返回
 * @param output 初始输出
 */
void bm_algo_pi_q15_reset(bm_algo_pi_q15_state_t *state, bm_algo_q15_t output);

/**
 * @brief 执行一拍 Q1.15 PI 控制并进行积分与输出限幅
 *
 * @param state 控制器状态
 * @param config PI 参数及限幅
 * @param error 本拍误差
 * @param dt_q15 Q1.15 时间步长，必须大于 0
 * @return 限幅后的 Q1.15 输出；参数无效或时间步长非正时返回 0
 */
bm_algo_q15_t bm_algo_pi_q15_step(bm_algo_pi_q15_state_t *state,
                                 const bm_algo_pi_q15_config_t *config,
                                 bm_algo_q15_t error,
                                 bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q31_t b0;
    bm_algo_q31_t b1;
    bm_algo_q31_t b2;
    bm_algo_q31_t a1;
    bm_algo_q31_t a2;
    bm_algo_q31_t out_min;
    bm_algo_q31_t out_max;
} bm_algo_pr_q31_config_t;

typedef struct {
    bm_algo_q31_t x1;
    bm_algo_q31_t x2;
    bm_algo_q31_t y1;
    bm_algo_q31_t y2;
    bm_algo_q31_t output;
} bm_algo_pr_q31_state_t;

/**
 * @brief 清零 Q1.31 PR 控制器历史状态
 *
 * @param state 控制器状态；NULL 时静默返回
 */
void bm_algo_pr_q31_reset(bm_algo_pr_q31_state_t *state);

/**
 * @brief 执行一拍 Q1.31 PR 差分控制并限幅
 *
 * @param state 控制器状态
 * @param config PR 差分系数及输出限幅
 * @param error 本拍误差
 * @return Q1.31 控制输出；state 或 config 为 NULL 时返回 0
 */
bm_algo_q31_t bm_algo_pr_q31_step(bm_algo_pr_q31_state_t *state,
                                  const bm_algo_pr_q31_config_t *config,
                                  bm_algo_q31_t error);

typedef struct {
    bm_algo_q15_t rate_per_s_q15;
} bm_algo_ramp_q15_config_t;

typedef struct {
    bm_algo_q15_t output;
    int done;
} bm_algo_ramp_q15_state_t;

/**
 * @brief 复位 Q1.15 斜坡发生器
 *
 * @param state 斜坡状态；NULL 时静默返回
 * @param output 初始输出
 */
void bm_algo_ramp_q15_reset(bm_algo_ramp_q15_state_t *state, bm_algo_q15_t output);

/**
 * @brief 以配置速率向 Q1.15 目标推进一拍
 *
 * @param state 斜坡状态
 * @param config Q1.15 每秒速率
 * @param target 目标值
 * @param dt_q15 Q1.15 时间步长，必须大于 0
 * @return 本拍 Q1.15 输出；参数无效时原样返回 target
 */
bm_algo_q15_t bm_algo_ramp_q15_step(bm_algo_ramp_q15_state_t *state,
                                    const bm_algo_ramp_q15_config_t *config,
                                    bm_algo_q15_t target,
                                    bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q15_t max_vel_q15;
    bm_algo_q15_t max_accel_q15;
    bm_algo_q15_t max_decel_q15;
} bm_algo_trapezoid_q15_config_t;

typedef struct {
    bm_algo_q15_t position;
    bm_algo_q15_t velocity;
    bm_algo_q15_t target;
    int done;
} bm_algo_trapezoid_q15_state_t;

/**
 * @brief 复位 Q1.15 梯形轨迹状态
 *
 * @param state 轨迹状态；NULL 时静默返回
 * @param position 初始位置
 * @param velocity 初始速度
 */
void bm_algo_trapezoid_q15_reset(bm_algo_trapezoid_q15_state_t *state,
                                 bm_algo_q15_t position,
                                 bm_algo_q15_t velocity);

/**
 * @brief 设置 Q1.15 梯形轨迹目标位置
 *
 * @param state 轨迹状态；NULL 时静默返回
 * @param target 目标位置
 */
void bm_algo_trapezoid_q15_set_target(bm_algo_trapezoid_q15_state_t *state,
                                      bm_algo_q15_t target);

/**
 * @brief 推进一拍 Q1.15 梯形速度轨迹
 *
 * @param state 轨迹状态
 * @param config 最大速度、加速度和减速度
 * @param dt_q15 Q1.15 时间步长，必须大于 0
 * @return 本拍位置；state/config 无效或时间步长非正时返回 0，约束无效时保持当前位置
 */
bm_algo_q15_t bm_algo_trapezoid_q15_step(bm_algo_trapezoid_q15_state_t *state,
                                         const bm_algo_trapezoid_q15_config_t *config,
                                         bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q15_t tolerance_abs;
    bm_algo_q15_t tolerance_rel;
} bm_algo_redundant_pair_q15_config_t;

/**
 * @brief 比较两路 Q1.15 冗余信号的一致性
 *
 * @param a 第一路信号
 * @param b 第二路信号
 * @param config 绝对与相对容差；NULL 时仅要求两路完全相等
 * @return 一致时返回 0；超差时返回 BM_ALGO_FAULT_REDUNDANT_MISMATCH
 */
uint32_t bm_algo_redundant_pair_q15_step(bm_algo_q15_t a,
                                         bm_algo_q15_t b,
                                         const bm_algo_redundant_pair_q15_config_t *config);

typedef struct {
    bm_algo_q31_t tolerance_abs;
    bm_algo_q31_t tolerance_rel;
} bm_algo_redundant_pair_q31_config_t;

/**
 * @brief 比较两路 Q1.31 冗余信号的一致性
 *
 * @param a 第一路信号
 * @param b 第二路信号
 * @param config 绝对与相对容差；NULL 时仅要求两路完全相等
 * @return 一致时返回 0；超差时返回 BM_ALGO_FAULT_REDUNDANT_MISMATCH
 */
uint32_t bm_algo_redundant_pair_q31_step(bm_algo_q31_t a,
                                         bm_algo_q31_t b,
                                         const bm_algo_redundant_pair_q31_config_t *config);

/** 滑动速率估计（Q15，窗口≤16 仅用于内部缓冲占位，E1 为一阶差分） */
typedef struct {
    bm_algo_q15_t prev_input;
    bm_algo_q15_t rate_per_s;
} bm_algo_rate_est_q15_state_t;

/**
 * @brief 复位 Q1.15 一阶速率估计器
 *
 * @param state 估计器状态；NULL 时静默返回
 * @param input 初始输入
 */
void bm_algo_rate_est_q15_reset(bm_algo_rate_est_q15_state_t *state,
                                bm_algo_q15_t input);

/**
 * @brief 根据相邻样本差分估计 Q1.15 每秒速率
 *
 * @param state 估计器状态
 * @param input 本拍输入
 * @param dt_q15 Q1.15 时间步长，必须大于 0
 * @return 饱和后的 Q1.15 速率；dt 无效时保持上次速率，state 为 NULL 时返回 0
 */
bm_algo_q15_t bm_algo_rate_est_q15_step(bm_algo_rate_est_q15_state_t *state,
                                        bm_algo_q15_t input,
                                        bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q15_t ocv_weight;
} bm_algo_soc_fusion_q15_config_t;

/**
 * @brief 按 OCV 权重融合两路 Q1.15 SOC
 *
 * @param soc_coulomb 库仑积分 SOC
 * @param soc_ocv 开路电压 SOC
 * @param config 含 Q1.15 OCV 权重的配置；NULL 时返回 soc_coulomb
 * @return 饱和后的 Q1.15 融合 SOC
 */
bm_algo_q15_t bm_algo_soc_fusion_q15_step(bm_algo_q15_t soc_coulomb,
                                        bm_algo_q15_t soc_ocv,
                                        const bm_algo_soc_fusion_q15_config_t *config);

/* ---------- 通用定点扩展（第十二批：电源/运动/信号质量） ---------- */
typedef struct {
    bm_algo_q15_t max_vel_q15;
    bm_algo_q15_t max_accel_q15;
    bm_algo_q15_t max_jerk_q15;
} bm_algo_scurve_q15_config_t;

typedef struct {
    bm_algo_q15_t position;
    bm_algo_q15_t velocity;
    bm_algo_q15_t acceleration;
    bm_algo_q15_t target;
    int done;
} bm_algo_scurve_q15_state_t;

/**
 * @brief 复位 Q1.15 S 曲线轨迹状态
 *
 * @param state 轨迹状态；NULL 时静默返回
 * @param position 初始位置
 * @param velocity 初始速度
 * @param acceleration 初始加速度
 */
void bm_algo_scurve_q15_reset(bm_algo_scurve_q15_state_t *state,
                              bm_algo_q15_t position,
                              bm_algo_q15_t velocity,
                              bm_algo_q15_t acceleration);

/**
 * @brief 设置 Q1.15 S 曲线目标位置
 *
 * @param state 轨迹状态；NULL 时静默返回
 * @param target 目标位置
 */
void bm_algo_scurve_q15_set_target(bm_algo_scurve_q15_state_t *state,
                                   bm_algo_q15_t target);

/**
 * @brief 通过浮点轨迹核推进一拍 Q1.15 S 曲线
 *
 * @param state 轨迹状态
 * @param config 最大速度、加速度和加加速度
 * @param dt_q15 Q1.15 时间步长，必须大于 0
 * @return 量化并饱和后的 Q1.15 位置；参数无效时返回当前位置，state 为 NULL 时返回 0
 */
bm_algo_q15_t bm_algo_scurve_q15_step(bm_algo_scurve_q15_state_t *state,
                                      const bm_algo_scurve_q15_config_t *config,
                                      bm_algo_q15_t dt_q15);

/** 滑动速率估计（Q31，一阶差分） */
typedef struct {
    bm_algo_q31_t prev_input;
    bm_algo_q31_t rate_per_s;
} bm_algo_rate_est_q31_state_t;

/**
 * @brief 复位 Q1.31 一阶速率估计器
 *
 * @param state 估计器状态；NULL 时静默返回
 * @param input 初始输入
 */
void bm_algo_rate_est_q31_reset(bm_algo_rate_est_q31_state_t *state,
                                bm_algo_q31_t input);

/**
 * @brief 根据相邻样本差分估计 Q1.31 每秒速率
 *
 * @param state 估计器状态
 * @param input 本拍输入
 * @param dt_q31 Q1.31 时间步长，必须大于 0
 * @return 饱和后的 Q1.31 速率；dt 无效时保持上次速率，state 为 NULL 时返回 0
 */
bm_algo_q31_t bm_algo_rate_est_q31_step(bm_algo_rate_est_q31_state_t *state,
                                        bm_algo_q31_t input,
                                        bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q31_t ocv_weight;
} bm_algo_soc_fusion_q31_config_t;

/**
 * @brief 按 OCV 权重融合两路 Q1.31 SOC
 *
 * @param soc_coulomb 库仑积分 SOC
 * @param soc_ocv 开路电压 SOC
 * @param config 含 Q1.31 OCV 权重的配置；NULL 时返回 soc_coulomb
 * @return 饱和后的 Q1.31 融合 SOC
 */
bm_algo_q31_t bm_algo_soc_fusion_q31_step(bm_algo_q31_t soc_coulomb,
                                          bm_algo_q31_t soc_ocv,
                                          const bm_algo_soc_fusion_q31_config_t *config);

typedef struct {
    bm_algo_q15_t step_v_q15;
    bm_algo_q15_t v_min_q15;
    bm_algo_q15_t v_max_q15;
} bm_algo_mppt_po_q15_config_t;

typedef struct {
    bm_algo_q15_t v_ref_q15;
    bm_algo_q15_t prev_power_q15;
    int           direction;
} bm_algo_mppt_po_q15_state_t;

/**
 * @brief 复位 Q1.15 扰动观察法 MPPT 状态
 *
 * @param state MPPT 状态；NULL 时静默返回
 * @param v_init_q15 初始参考电压
 */
void bm_algo_mppt_po_q15_reset(bm_algo_mppt_po_q15_state_t *state,
                               bm_algo_q15_t v_init_q15);

/**
 * @brief 执行一拍 Q1.15 扰动观察法 MPPT
 *
 * @param state MPPT 状态
 * @param config 步长及参考电压限幅
 * @param voltage_q15 光伏电压
 * @param current_q15 光伏电流
 * @return 限幅后的参考电压；state 或 config 为 NULL 时返回 voltage_q15
 */
bm_algo_q15_t bm_algo_mppt_po_q15_step(bm_algo_mppt_po_q15_state_t *state,
                                     const bm_algo_mppt_po_q15_config_t *config,
                                     bm_algo_q15_t voltage_q15,
                                     bm_algo_q15_t current_q15);

typedef struct {
    bm_algo_q15_t step_v_q15;
    bm_algo_q15_t v_min_q15;
    bm_algo_q15_t v_max_q15;
} bm_algo_mppt_ic_q15_config_t;

typedef struct {
    bm_algo_q15_t v_ref_q15;
    bm_algo_q15_t prev_v_q15;
    bm_algo_q15_t prev_i_q15;
} bm_algo_mppt_ic_q15_state_t;

/**
 * @brief 复位 Q1.15 增量电导法 MPPT 状态
 *
 * @param state MPPT 状态；NULL 时静默返回
 * @param v_init_q15 初始参考电压
 */
void bm_algo_mppt_ic_q15_reset(bm_algo_mppt_ic_q15_state_t *state,
                               bm_algo_q15_t v_init_q15);

/**
 * @brief 执行一拍 Q1.15 增量电导法 MPPT
 *
 * @param state MPPT 状态
 * @param config 步长及参考电压限幅
 * @param voltage_q15 光伏电压
 * @param current_q15 光伏电流
 * @return 限幅后的参考电压；state 或 config 为 NULL 时返回 voltage_q15
 */
bm_algo_q15_t bm_algo_mppt_ic_q15_step(bm_algo_mppt_ic_q15_state_t *state,
                                     const bm_algo_mppt_ic_q15_config_t *config,
                                     bm_algo_q15_t voltage_q15,
                                     bm_algo_q15_t current_q15);

typedef struct {
    bm_algo_q15_t min_v_q15;
    bm_algo_q15_t max_v_q15;
    bm_algo_q15_t max_rate_per_s_q15;
} bm_algo_range_monitor_q15_config_t;

typedef struct {
    bm_algo_q15_t prev_q15;
    uint32_t      fault_flags;
} bm_algo_range_monitor_q15_state_t;

/**
 * @brief 复位 Q1.15 范围监控器并清除故障
 *
 * @param state 监控状态；NULL 时静默返回
 * @param v_q15 初始样本
 */
void bm_algo_range_monitor_q15_reset(bm_algo_range_monitor_q15_state_t *state,
                                     bm_algo_q15_t v_q15);

/**
 * @brief 检测 Q1.15 样本的越界、变化率和冻结故障
 *
 * @param state 监控状态
 * @param config 范围与最大变化率配置
 * @param sample_q15 本拍样本
 * @param dt_q15 Q1.15 时间步长
 * @return 本拍累计故障位掩码；state 或 config 为 NULL 时返回 0
 */
uint32_t bm_algo_range_monitor_q15_step(
    bm_algo_range_monitor_q15_state_t *state,
    const bm_algo_range_monitor_q15_config_t *config,
    bm_algo_q15_t sample_q15,
    bm_algo_q15_t dt_q15);

typedef struct {
    uint32_t      stable_count_required;
    bm_algo_q15_t tolerance_q15;
} bm_algo_debounce_analog_q15_config_t;

typedef struct {
    bm_algo_q15_t candidate_q15;
    uint32_t      stable_count;
    bm_algo_q15_t latched_q15;
    int           valid;
} bm_algo_debounce_analog_q15_state_t;

/**
 * @brief 复位 Q1.15 模拟量去抖状态
 *
 * @param state 去抖状态；NULL 时静默返回
 * @param initial_q15 初始锁存值
 */
void bm_algo_debounce_analog_q15_reset(bm_algo_debounce_analog_q15_state_t *state,
                                       bm_algo_q15_t initial_q15);

/**
 * @brief 更新 Q1.15 模拟量去抖状态
 *
 * @param state 去抖状态
 * @param config 稳定计数与容差
 * @param sample_q15 本拍样本
 * @return 稳定后更新锁存值并返回 1；未稳定或参数无效时返回 0
 */
int bm_algo_debounce_analog_q15_step(
    bm_algo_debounce_analog_q15_state_t *state,
    const bm_algo_debounce_analog_q15_config_t *config,
    bm_algo_q15_t sample_q15);

typedef struct {
    bm_algo_q31_t accumulated_wh_q31;
} bm_algo_energy_wh_q15_state_t;

/**
 * @brief 清零以 Q1.31 保存的 Q1.15 电能积分状态
 *
 * @param state 电能状态；NULL 时静默返回
 */
void bm_algo_energy_wh_q15_reset(bm_algo_energy_wh_q15_state_t *state);

/**
 * @brief 积分 Q1.15 功率与时间步长并累加 Q1.31 电能
 *
 * @param state 电能状态
 * @param p_q15 Q1.15 有功功率
 * @param dt_q15 Q1.15 时间步长，必须大于 0
 * @return 饱和后的累计 Q1.31 电能；dt 无效时保持累计值，state 为 NULL 时返回 0
 */
bm_algo_q31_t bm_algo_energy_wh_integrator_q15_step(
    bm_algo_energy_wh_q15_state_t *state,
    bm_algo_q15_t p_q15,
    bm_algo_q15_t dt_q15);

/* ---------- 通用定点扩展（第十四批：全族 Q15/Q31 收口） ---------- */
typedef struct {
    bm_algo_q31_t alpha_q31;
} bm_algo_complementary_q31_config_t;

typedef struct {
    bm_algo_q31_t roll_rad;
    bm_algo_q31_t pitch_rad;
} bm_algo_complementary_q31_state_t;

/**
 * @brief 清零 Q1.31 互补姿态滤波状态
 *
 * @param state 姿态状态；NULL 时静默返回
 */
void bm_algo_complementary_q31_reset(bm_algo_complementary_q31_state_t *state);

/**
 * @brief 通过浮点 atan2 桥接更新 Q1.31 横滚角和俯仰角
 *
 * @param state 姿态状态；NULL 时静默返回
 * @param config 含 Q1.31 融合系数的配置；NULL 时静默返回
 * @param gx_q31 X 轴 Q1.31 角速度
 * @param gy_q31 Y 轴 Q1.31 角速度
 * @param gz_q31 Z 轴 Q1.31 角速度，当前实现忽略
 * @param ax_q31 X 轴 Q1.31 加速度
 * @param ay_q31 Y 轴 Q1.31 加速度
 * @param az_q31 Z 轴 Q1.31 加速度
 * @param dt_q31 Q1.31 时间步长；非正时静默返回
 */
void bm_algo_complementary_q31_step(bm_algo_complementary_q31_state_t *state,
                                    const bm_algo_complementary_q31_config_t *config,
                                    bm_algo_q31_t gx_q31,
                                    bm_algo_q31_t gy_q31,
                                    bm_algo_q31_t gz_q31,
                                    bm_algo_q31_t ax_q31,
                                    bm_algo_q31_t ay_q31,
                                    bm_algo_q31_t az_q31,
                                    bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q15_t x0_q15;
    bm_algo_q15_t y0_q15;
    bm_algo_q15_t x1_q15;
    bm_algo_q15_t y1_q15;
    bm_algo_q15_t step_size_q15;
} bm_algo_dda_q15_config_t;

typedef struct {
    bm_algo_q15_t x_q15;
    bm_algo_q15_t y_q15;
    int done;
    float x;
    float y;
    float err;
    int step_x;
    int step_y;
    float dx;
    float dy;
    float target_x;
    float target_y;
    float step_size;
    uint32_t steps;
    uint32_t step_count;
} bm_algo_dda_q15_state_t;

/**
 * @brief 按 Q1.15 端点配置复位浮点桥接 DDA 状态
 *
 * @param state DDA 状态；NULL 时静默返回
 * @param config 起止点与步长；NULL 时静默返回
 */
void bm_algo_dda_q15_reset(bm_algo_dda_q15_state_t *state,
                          const bm_algo_dda_q15_config_t *config);

/**
 * @brief 推进一拍浮点桥接的 Q1.15 DDA 直线插补
 *
 * @param state DDA 状态
 * @param config 起止点与步长
 * @param x_out_q15 可选 X 坐标输出；可为 NULL
 * @param y_out_q15 可选 Y 坐标输出；可为 NULL
 * @return 成功推进一个插补点返回 1；已完成、配置不匹配或参数无效时返回 0
 */
int bm_algo_dda_q15_step(bm_algo_dda_q15_state_t *state,
                         const bm_algo_dda_q15_config_t *config,
                         bm_algo_q15_t *x_out_q15,
                         bm_algo_q15_t *y_out_q15);

typedef struct {
    bm_algo_q31_t x0_q31;
    bm_algo_q31_t y0_q31;
    bm_algo_q31_t x1_q31;
    bm_algo_q31_t y1_q31;
    bm_algo_q31_t step_size_q31;
} bm_algo_dda_q31_config_t;

typedef struct {
    bm_algo_q31_t x_q31;
    bm_algo_q31_t y_q31;
    int done;
    float x;
    float y;
    float err;
    int step_x;
    int step_y;
    float dx;
    float dy;
    float target_x;
    float target_y;
    float step_size;
    uint32_t steps;
    uint32_t step_count;
} bm_algo_dda_q31_state_t;

/**
 * @brief 按 Q1.31 端点配置复位浮点桥接 DDA 状态
 *
 * @param state DDA 状态；NULL 时静默返回
 * @param config 起止点与步长；NULL 时静默返回
 */
void bm_algo_dda_q31_reset(bm_algo_dda_q31_state_t *state,
                          const bm_algo_dda_q31_config_t *config);

/**
 * @brief 推进一拍浮点桥接的 Q1.31 DDA 直线插补
 *
 * @param state DDA 状态
 * @param config 起止点与步长
 * @param x_out_q31 可选 X 坐标输出；可为 NULL
 * @param y_out_q31 可选 Y 坐标输出；可为 NULL
 * @return 成功推进一个插补点返回 1；已完成、配置不匹配或参数无效时返回 0
 */
int bm_algo_dda_q31_step(bm_algo_dda_q31_state_t *state,
                         const bm_algo_dda_q31_config_t *config,
                         bm_algo_q31_t *x_out_q31,
                         bm_algo_q31_t *y_out_q31);

typedef struct {
    uint32_t stable_count_required;
    bm_algo_q31_t tolerance_q31;
} bm_algo_debounce_analog_q31_config_t;

typedef struct {
    bm_algo_q31_t candidate_q31;
    uint32_t      stable_count;
    bm_algo_q31_t latched_q31;
    int           valid;
} bm_algo_debounce_analog_q31_state_t;

/**
 * @brief 复位 Q1.31 模拟量去抖状态
 *
 * @param state 去抖状态；NULL 时静默返回
 * @param initial_q31 初始锁存值
 */
void bm_algo_debounce_analog_q31_reset(bm_algo_debounce_analog_q31_state_t *state,
                                       bm_algo_q31_t initial_q31);

/**
 * @brief 更新 Q1.31 模拟量去抖状态
 *
 * @param state 去抖状态
 * @param config 稳定计数与容差
 * @param sample_q31 本拍样本
 * @return 稳定后更新锁存值并返回 1；未稳定或参数无效时返回 0
 */
int bm_algo_debounce_analog_q31_step(
    bm_algo_debounce_analog_q31_state_t *state,
    const bm_algo_debounce_analog_q31_config_t *config,
    bm_algo_q31_t sample_q31);

typedef struct {
    uint32_t decim;
    uint32_t counter;
} bm_algo_decimator_q15_state_t;

/**
 * @brief 复位 Q1.15 整数抽取计数器
 *
 * @param state 抽取状态；NULL 时静默返回
 */
void bm_algo_decimator_q15_reset(bm_algo_decimator_q15_state_t *state);

/**
 * @brief 按整数因子抽取 Q1.15 样本
 *
 * @param state 抽取状态
 * @param decim 抽取因子，必须大于 0
 * @param input_q15 本拍输入
 * @param output_q15 命中抽取拍时写入的可选输出
 * @return 本拍产生输出时返回 1，否则返回 0；state 为 NULL 或 decim 为 0 时返回 0
 */
int bm_algo_decimator_q15_step(bm_algo_decimator_q15_state_t *state,
                               uint32_t decim,
                               bm_algo_q15_t input_q15,
                               bm_algo_q15_t *output_q15);

typedef struct {
    uint32_t decim;
    uint32_t counter;
} bm_algo_decimator_q31_state_t;

/**
 * @brief 复位 Q1.31 整数抽取计数器
 *
 * @param state 抽取状态；NULL 时静默返回
 */
void bm_algo_decimator_q31_reset(bm_algo_decimator_q31_state_t *state);

/**
 * @brief 按整数因子抽取 Q1.31 样本
 *
 * @param state 抽取状态
 * @param decim 抽取因子，必须大于 0
 * @param input_q31 本拍输入
 * @param output_q31 命中抽取拍时写入的可选输出
 * @return 本拍产生输出时返回 1，否则返回 0；state 为 NULL 或 decim 为 0 时返回 0
 */
int bm_algo_decimator_q31_step(bm_algo_decimator_q31_state_t *state,
                               uint32_t decim,
                               bm_algo_q31_t input_q31,
                               bm_algo_q31_t *output_q31);

typedef struct {
    int32_t max_delta_per_step;
} bm_algo_encoder_diag_q15_config_t;

typedef struct {
    int32_t prev_count;
} bm_algo_encoder_diag_q15_state_t;

/**
 * @brief 复位 Q15 包装的编码器诊断计数状态
 *
 * @param state 诊断状态；NULL 时静默返回
 * @param raw_count 初始原始计数
 */
void bm_algo_encoder_diag_q15_reset(bm_algo_encoder_diag_q15_state_t *state,
                                    int32_t raw_count);

/**
 * @brief 检测编码器计数跳变与索引脉冲故障
 *
 * @param state 诊断状态
 * @param config 每拍最大允许计数变化
 * @param raw_count 本拍原始计数
 * @param index_pulse_seen 本拍是否检测到索引脉冲
 * @return 编码器故障位掩码；state 或 config 为 NULL 时返回 BM_ALGO_ENCODER_FAULT_NONE
 */
uint32_t bm_algo_encoder_diag_q15_step(bm_algo_encoder_diag_q15_state_t *state,
                                     const bm_algo_encoder_diag_q15_config_t *config,
                                     int32_t raw_count,
                                     int index_pulse_seen);

typedef struct {
    int32_t max_delta_per_step;
} bm_algo_encoder_diag_q31_config_t;

typedef struct {
    int32_t prev_count;
} bm_algo_encoder_diag_q31_state_t;

/**
 * @brief 复位 Q31 包装的编码器诊断计数状态
 *
 * @param state 诊断状态；NULL 时静默返回
 * @param raw_count 初始原始计数
 */
void bm_algo_encoder_diag_q31_reset(bm_algo_encoder_diag_q31_state_t *state,
                                    int32_t raw_count);

/**
 * @brief 检测编码器计数跳变与索引脉冲故障
 *
 * @param state 诊断状态
 * @param config 每拍最大允许计数变化
 * @param raw_count 本拍原始计数
 * @param index_pulse_seen 本拍是否检测到索引脉冲
 * @return 编码器故障位掩码；state 或 config 为 NULL 时返回 BM_ALGO_ENCODER_FAULT_NONE
 */
uint32_t bm_algo_encoder_diag_q31_step(bm_algo_encoder_diag_q31_state_t *state,
                                     const bm_algo_encoder_diag_q31_config_t *config,
                                     int32_t raw_count,
                                     int index_pulse_seen);

typedef struct {
    bm_algo_q31_t accumulated_wh_q31;
} bm_algo_energy_wh_q31_state_t;

/**
 * @brief 清零 Q1.31 电能积分状态
 *
 * @param state 电能状态；NULL 时静默返回
 */
void bm_algo_energy_wh_q31_reset(bm_algo_energy_wh_q31_state_t *state);

/**
 * @brief 积分 Q1.31 功率与时间步长
 *
 * @param state 电能状态
 * @param p_q31 Q1.31 有功功率
 * @param dt_q31 Q1.31 时间步长，必须大于 0
 * @return 饱和后的累计 Q1.31 电能；dt 无效时保持累计值，state 为 NULL 时返回 0
 */
bm_algo_q31_t bm_algo_energy_wh_integrator_q31_step(
    bm_algo_energy_wh_q31_state_t *state,
    bm_algo_q31_t p_q31,
    bm_algo_q31_t dt_q31);

#define BM_ALGO_FIR_Q15_MAX_TAPS  8u

typedef struct {
    const bm_algo_q15_t *coeffs;
    uint8_t              tap_count;
    bm_algo_q15_t       *delay_line;
} bm_algo_fir_q15_config_t;

typedef struct {
    uint8_t index;
    uint8_t tap_count;
} bm_algo_fir_q15_state_t;

/**
 * @brief 初始化并清零 Q1.15 FIR 状态与外部延迟线
 *
 * @param state FIR 状态
 * @param config 系数、抽头数及外部延迟线
 * @return BM_OK 成功；指针、抽头数或容量约束无效时返回 BM_ERR_INVALID
 */
int bm_algo_fir_q15_init(bm_algo_fir_q15_state_t *state,
                         const bm_algo_fir_q15_config_t *config);

/**
 * @brief 清零 Q1.15 FIR 外部延迟线和游标
 *
 * @param state FIR 状态；NULL 时静默返回
 * @param config 含外部延迟线的配置；无效时静默返回
 */
void bm_algo_fir_q15_reset(bm_algo_fir_q15_state_t *state,
                           const bm_algo_fir_q15_config_t *config);

/**
 * @brief 执行一拍 Q1.15 FIR 卷积
 *
 * @param state FIR 状态
 * @param config 系数、抽头数及外部延迟线
 * @param input_q15 本拍输入
 * @return 饱和后的 Q1.15 输出；配置无效时原样返回 input_q15
 */
bm_algo_q15_t bm_algo_fir_q15_step(bm_algo_fir_q15_state_t *state,
                                   const bm_algo_fir_q15_config_t *config,
                                   bm_algo_q15_t input_q15);

#define BM_ALGO_FIR_Q31_MAX_TAPS  8u

typedef struct {
    const bm_algo_q31_t *coeffs;
    uint8_t              tap_count;
    bm_algo_q31_t       *delay_line;
} bm_algo_fir_q31_config_t;

typedef struct {
    uint8_t index;
    uint8_t tap_count;
} bm_algo_fir_q31_state_t;

/**
 * @brief 初始化并清零 Q1.31 FIR 状态与外部延迟线
 *
 * @param state FIR 状态
 * @param config 系数、抽头数及外部延迟线
 * @return BM_OK 成功；指针、抽头数或容量约束无效时返回 BM_ERR_INVALID
 */
int bm_algo_fir_q31_init(bm_algo_fir_q31_state_t *state,
                         const bm_algo_fir_q31_config_t *config);

/**
 * @brief 清零 Q1.31 FIR 外部延迟线和游标
 *
 * @param state FIR 状态；NULL 时静默返回
 * @param config 含外部延迟线的配置；无效时静默返回
 */
void bm_algo_fir_q31_reset(bm_algo_fir_q31_state_t *state,
                           const bm_algo_fir_q31_config_t *config);

/**
 * @brief 执行一拍 Q1.31 FIR 卷积
 *
 * @param state FIR 状态
 * @param config 系数、抽头数及外部延迟线
 * @param input_q31 本拍输入
 * @return 饱和后的 Q1.31 输出；配置无效时原样返回 input_q31
 */
bm_algo_q31_t bm_algo_fir_q31_step(bm_algo_fir_q31_state_t *state,
                                   const bm_algo_fir_q31_config_t *config,
                                   bm_algo_q31_t input_q31);

typedef struct {
    bm_algo_q15_t rs_q15;       /**< 定子电阻（Q15 归一化） */
    bm_algo_q15_t ls_q15;       /**< 定子电感（Q15 归一化） */
    bm_algo_q15_t pll_kp_q15;   /**< PLL 比例增益（Q15 归一化） */
    bm_algo_q15_t pll_ki_q15;   /**< PLL 积分增益（Q15 归一化） */
    /**
     * @brief 磁链衰减积分截止频率（rad/s，float 物理量）
     *
     * 用于带衰减积分：flux = flux*(1 - wc*dt) + v_emf*dt，
     * 消除纯积分在低速/静止时的 DC 偏置漂移。
     * 典型取值：5～30 rad/s（对应截止频率约 0.8～5 Hz）；
     * 设为 0.0f 时退化为纯积分（向后兼容默认值）。
     */
    float wc_rad_s;
} bm_algo_flux_observer_q15_config_t;

typedef struct {
    bm_algo_q15_t theta_rad_q15;
    bm_algo_q15_t omega_rad_s_q15;
    float theta_rad;
    float omega_rad_s;
    float flux_alpha;
    float flux_beta;
} bm_algo_flux_observer_q15_state_t;

/**
 * @brief 复位 Q1.15 磁链观测器及浮点桥接状态
 *
 * @param state 观测器状态；NULL 时静默返回
 * @param theta_rad_q15 初始 Q1.15 电角度
 */
void bm_algo_flux_observer_q15_reset(bm_algo_flux_observer_q15_state_t *state,
                                     bm_algo_q15_t theta_rad_q15);

/**
 * @brief 通过浮点磁链与 PLL 核更新 Q1.15 转子角度
 *
 * @param state 观测器状态
 * @param config 电机参数、PLL 增益与衰减频率
 * @param v_alpha_q15 Q1.15 α 轴电压
 * @param v_beta_q15 Q1.15 β 轴电压
 * @param i_alpha_q15 Q1.15 α 轴电流
 * @param i_beta_q15 Q1.15 β 轴电流
 * @param dt_q15 Q1.15 时间步长，必须大于 0
 * @return 量化并饱和后的 Q1.15 电角度；参数无效时返回 0
 */
bm_algo_q15_t bm_algo_flux_observer_q15_step(
    bm_algo_flux_observer_q15_state_t *state,
    const bm_algo_flux_observer_q15_config_t *config,
    bm_algo_q15_t v_alpha_q15,
    bm_algo_q15_t v_beta_q15,
    bm_algo_q15_t i_alpha_q15,
    bm_algo_q15_t i_beta_q15,
    bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q31_t rs_q31;       /**< 定子电阻（Q31 归一化） */
    bm_algo_q31_t ls_q31;       /**< 定子电感（Q31 归一化） */
    bm_algo_q31_t pll_kp_q31;   /**< PLL 比例增益（Q31 归一化） */
    bm_algo_q31_t pll_ki_q31;   /**< PLL 积分增益（Q31 归一化） */
    /**
     * @brief 磁链衰减积分截止频率（rad/s，float 物理量）
     *
     * 用于带衰减积分：flux = flux*(1 - wc*dt) + v_emf*dt，
     * 消除纯积分在低速/静止时的 DC 偏置漂移。
     * 典型取值：5～30 rad/s（对应截止频率约 0.8～5 Hz）；
     * 设为 0.0f 时退化为纯积分（向后兼容默认值）。
     */
    float wc_rad_s;
} bm_algo_flux_observer_q31_config_t;

typedef struct {
    bm_algo_q31_t theta_rad_q31;
    bm_algo_q31_t omega_rad_s_q31;
    float theta_rad;
    float omega_rad_s;
    float flux_alpha;
    float flux_beta;
} bm_algo_flux_observer_q31_state_t;

/**
 * @brief 复位 Q1.31 磁链观测器及浮点桥接状态
 *
 * @param state 观测器状态；NULL 时静默返回
 * @param theta_rad_q31 初始 Q1.31 电角度
 */
void bm_algo_flux_observer_q31_reset(bm_algo_flux_observer_q31_state_t *state,
                                     bm_algo_q31_t theta_rad_q31);

/**
 * @brief 通过浮点磁链与 PLL 核更新 Q1.31 转子角度
 *
 * @param state 观测器状态
 * @param config 电机参数、PLL 增益与衰减频率
 * @param v_alpha_q31 Q1.31 α 轴电压
 * @param v_beta_q31 Q1.31 β 轴电压
 * @param i_alpha_q31 Q1.31 α 轴电流
 * @param i_beta_q31 Q1.31 β 轴电流
 * @param dt_q31 Q1.31 时间步长，必须大于 0
 * @return 量化并饱和后的 Q1.31 电角度；参数无效时返回 0
 */
bm_algo_q31_t bm_algo_flux_observer_q31_step(
    bm_algo_flux_observer_q31_state_t *state,
    const bm_algo_flux_observer_q31_config_t *config,
    bm_algo_q31_t v_alpha_q31,
    bm_algo_q31_t v_beta_q31,
    bm_algo_q31_t i_alpha_q31,
    bm_algo_q31_t i_beta_q31,
    bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q15_t ratio_q15;        /**< 输出/输入采样率比，标准 Q15（±1.0 定标域） */
    bm_algo_q15_t phase_q15;        /**< 重采样相位，专用定标（非标准 Q15 ±1.0 域，
                                      *   缩放系数 1024，可表达 |phase|≤32；见
                                      *   bm_algo_fixed.c resample_phase_q15_from_float()），
                                      *   降采样（ratio<1）时可能 >1.0，勿用
                                      *   bm_algo_q15_to_float() 解释此字段 */
    bm_algo_q15_t prev_sample_q15;  /**< 上一个输入样本，标准 Q15（±1.0 定标域） */
} bm_algo_linear_resampler_q15_state_t;

/**
 * @brief 复位 Q1.15 线性重采样器
 *
 * @param state 重采样状态；NULL 时静默返回
 * @param ratio_q15 标准 Q1.15 输出/输入采样率比
 * @param initial_q15 初始输入样本
 */
void bm_algo_linear_resampler_q15_reset(bm_algo_linear_resampler_q15_state_t *state,
                                        bm_algo_q15_t ratio_q15,
                                        bm_algo_q15_t initial_q15);

/**
 * @brief 通过浮点桥接执行一拍 Q1.15 线性重采样
 *
 * @param state 重采样状态
 * @param input_q15 本拍 Q1.15 输入
 * @param outputs_q15 输出数组；不可为 NULL
 * @param max_outputs 输出容量，内部最多使用 8
 * @param out_count 实际输出数量；不可为 NULL，失败时置 0
 * @return 非负值为输出样本数；容量不足返回 BM_ERR_OVERFLOW，比例或容量无效返回 BM_ERR_INVALID；必要指针为 NULL 时返回 0
 */
int bm_algo_linear_resampler_q15_step(bm_algo_linear_resampler_q15_state_t *state,
                                      bm_algo_q15_t input_q15,
                                      bm_algo_q15_t *outputs_q15,
                                      uint32_t max_outputs,
                                      uint32_t *out_count);

typedef struct {
    bm_algo_q31_t ratio_q31;        /**< 输出/输入采样率比，标准 Q31（±1.0 定标域） */
    bm_algo_q31_t phase_q31;        /**< 重采样相位，专用定标（非标准 Q31 ±1.0 域，
                                      *   缩放系数 2^24，可表达 |phase|≤128；见
                                      *   bm_algo_fixed.c resample_phase_q31_from_float()），
                                      *   降采样（ratio<1）时可能 >1.0，勿用
                                      *   bm_algo_q31_to_float() 解释此字段 */
    bm_algo_q31_t prev_sample_q31;  /**< 上一个输入样本，标准 Q31（±1.0 定标域） */
} bm_algo_linear_resampler_q31_state_t;

/**
 * @brief 复位 Q1.31 线性重采样器
 *
 * @param state 重采样状态；NULL 时静默返回
 * @param ratio_q31 标准 Q1.31 输出/输入采样率比
 * @param initial_q31 初始输入样本
 */
void bm_algo_linear_resampler_q31_reset(bm_algo_linear_resampler_q31_state_t *state,
                                        bm_algo_q31_t ratio_q31,
                                        bm_algo_q31_t initial_q31);

/**
 * @brief 通过浮点桥接执行一拍 Q1.31 线性重采样
 *
 * @param state 重采样状态
 * @param input_q31 本拍 Q1.31 输入
 * @param outputs_q31 输出数组；不可为 NULL
 * @param max_outputs 输出容量，内部最多使用 8
 * @param out_count 实际输出数量；不可为 NULL，失败时置 0
 * @return 非负值为输出样本数；容量不足返回 BM_ERR_OVERFLOW，比例或容量无效返回 BM_ERR_INVALID；必要指针为 NULL 时返回 0
 */
int bm_algo_linear_resampler_q31_step(bm_algo_linear_resampler_q31_state_t *state,
                                      bm_algo_q31_t input_q31,
                                      bm_algo_q31_t *outputs_q31,
                                      uint32_t max_outputs,
                                      uint32_t *out_count);

typedef struct {
    bm_algo_q15_t beta_q15;
} bm_algo_madgwick_q15_config_t;

typedef struct {
    bm_algo_q15_t qw_q15;
    bm_algo_q15_t qx_q15;
    bm_algo_q15_t qy_q15;
    bm_algo_q15_t qz_q15;
} bm_algo_madgwick_q15_state_t;

/**
 * @brief 将 Q1.15 Madgwick 四元数复位为单位姿态
 *
 * @param state 姿态状态；NULL 时静默返回
 */
void bm_algo_madgwick_q15_reset(bm_algo_madgwick_q15_state_t *state);

/**
 * @brief 通过浮点桥接执行一拍 Q1.15 Madgwick 姿态融合
 *
 * @param state 姿态状态；NULL 时静默返回
 * @param config 含 Q1.15 beta 的配置；NULL 时静默返回
 * @param gx_q15 X 轴角速度
 * @param gy_q15 Y 轴角速度
 * @param gz_q15 Z 轴角速度
 * @param ax_q15 X 轴加速度
 * @param ay_q15 Y 轴加速度
 * @param az_q15 Z 轴加速度
 * @param dt_q15 Q1.15 时间步长；非正时静默返回
 */
void bm_algo_madgwick_q15_step(bm_algo_madgwick_q15_state_t *state,
                               const bm_algo_madgwick_q15_config_t *config,
                               bm_algo_q15_t gx_q15,
                               bm_algo_q15_t gy_q15,
                               bm_algo_q15_t gz_q15,
                               bm_algo_q15_t ax_q15,
                               bm_algo_q15_t ay_q15,
                               bm_algo_q15_t az_q15,
                               bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q31_t beta_q31;
} bm_algo_madgwick_q31_config_t;

typedef struct {
    bm_algo_q31_t qw_q31;
    bm_algo_q31_t qx_q31;
    bm_algo_q31_t qy_q31;
    bm_algo_q31_t qz_q31;
} bm_algo_madgwick_q31_state_t;

/**
 * @brief 将 Q1.31 Madgwick 四元数复位为单位姿态
 *
 * @param state 姿态状态；NULL 时静默返回
 */
void bm_algo_madgwick_q31_reset(bm_algo_madgwick_q31_state_t *state);

/**
 * @brief 通过浮点桥接执行一拍 Q1.31 Madgwick 姿态融合
 *
 * @param state 姿态状态；NULL 时静默返回
 * @param config 含 Q1.31 beta 的配置；NULL 时静默返回
 * @param gx_q31 X 轴角速度
 * @param gy_q31 Y 轴角速度
 * @param gz_q31 Z 轴角速度
 * @param ax_q31 X 轴加速度
 * @param ay_q31 Y 轴加速度
 * @param az_q31 Z 轴加速度
 * @param dt_q31 Q1.31 时间步长；非正时静默返回
 */
void bm_algo_madgwick_q31_step(bm_algo_madgwick_q31_state_t *state,
                               const bm_algo_madgwick_q31_config_t *config,
                               bm_algo_q31_t gx_q31,
                               bm_algo_q31_t gy_q31,
                               bm_algo_q31_t gz_q31,
                               bm_algo_q31_t ax_q31,
                               bm_algo_q31_t ay_q31,
                               bm_algo_q31_t az_q31,
                               bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q15_t kp_q15;
    bm_algo_q15_t ki_q15;
} bm_algo_mahony_q15_config_t;

typedef struct {
    bm_algo_q15_t qw_q15;      /**< 四元数 w 分量（Q15） */
    bm_algo_q15_t qx_q15;      /**< 四元数 x 分量（Q15） */
    bm_algo_q15_t qy_q15;      /**< 四元数 y 分量（Q15） */
    bm_algo_q15_t qz_q15;      /**< 四元数 z 分量（Q15） */
    float integral_x;           /**< Ki 积分项 x（浮点保存帧间状态，对应 bm_algo_mahony_state_t.integral_x） */
    float integral_y;           /**< Ki 积分项 y（浮点保存帧间状态） */
    float integral_z;           /**< Ki 积分项 z（浮点保存帧间状态） */
} bm_algo_mahony_q15_state_t;

/**
 * @brief 将 Q1.15 Mahony 四元数与浮点积分项复位
 *
 * @param state 姿态状态；NULL 时静默返回
 */
void bm_algo_mahony_q15_reset(bm_algo_mahony_q15_state_t *state);

/**
 * @brief 通过浮点桥接执行一拍 Q1.15 Mahony 姿态融合
 *
 * @param state 姿态状态；NULL 时静默返回
 * @param config 含 Q1.15 kp/ki 的配置；NULL 时静默返回
 * @param gx_q15 X 轴角速度
 * @param gy_q15 Y 轴角速度
 * @param gz_q15 Z 轴角速度
 * @param ax_q15 X 轴加速度
 * @param ay_q15 Y 轴加速度
 * @param az_q15 Z 轴加速度
 * @param dt_q15 Q1.15 时间步长；非正时静默返回
 */
void bm_algo_mahony_q15_step(bm_algo_mahony_q15_state_t *state,
                             const bm_algo_mahony_q15_config_t *config,
                             bm_algo_q15_t gx_q15,
                             bm_algo_q15_t gy_q15,
                             bm_algo_q15_t gz_q15,
                             bm_algo_q15_t ax_q15,
                             bm_algo_q15_t ay_q15,
                             bm_algo_q15_t az_q15,
                             bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q31_t kp_q31;
    bm_algo_q31_t ki_q31;
} bm_algo_mahony_q31_config_t;

typedef struct {
    bm_algo_q31_t qw_q31;      /**< 四元数 w 分量（Q31） */
    bm_algo_q31_t qx_q31;      /**< 四元数 x 分量（Q31） */
    bm_algo_q31_t qy_q31;      /**< 四元数 y 分量（Q31） */
    bm_algo_q31_t qz_q31;      /**< 四元数 z 分量（Q31） */
    float integral_x;           /**< Ki 积分项 x（浮点保存帧间状态，对应 bm_algo_mahony_state_t.integral_x） */
    float integral_y;           /**< Ki 积分项 y（浮点保存帧间状态） */
    float integral_z;           /**< Ki 积分项 z（浮点保存帧间状态） */
} bm_algo_mahony_q31_state_t;

/**
 * @brief 将 Q1.31 Mahony 四元数与浮点积分项复位
 *
 * @param state 姿态状态；NULL 时静默返回
 */
void bm_algo_mahony_q31_reset(bm_algo_mahony_q31_state_t *state);

/**
 * @brief 通过浮点桥接执行一拍 Q1.31 Mahony 姿态融合
 *
 * @param state 姿态状态；NULL 时静默返回
 * @param config 含 Q1.31 kp/ki 的配置；NULL 时静默返回
 * @param gx_q31 X 轴角速度
 * @param gy_q31 Y 轴角速度
 * @param gz_q31 Z 轴角速度
 * @param ax_q31 X 轴加速度
 * @param ay_q31 Y 轴加速度
 * @param az_q31 Z 轴加速度
 * @param dt_q31 Q1.31 时间步长；非正时静默返回
 */
void bm_algo_mahony_q31_step(bm_algo_mahony_q31_state_t *state,
                             const bm_algo_mahony_q31_config_t *config,
                             bm_algo_q31_t gx_q31,
                             bm_algo_q31_t gy_q31,
                             bm_algo_q31_t gz_q31,
                             bm_algo_q31_t ax_q31,
                             bm_algo_q31_t ay_q31,
                             bm_algo_q31_t az_q31,
                             bm_algo_q31_t dt_q31);

#define BM_ALGO_MEDIAN_Q15_MAX  16u

typedef struct {
    uint16_t window_size;
} bm_algo_median_q15_config_t;

typedef struct {
    bm_algo_q15_t samples[BM_ALGO_MEDIAN_Q15_MAX];
    uint16_t      count;
    uint16_t      index;
} bm_algo_median_q15_state_t;

/**
 * @brief 清零 Q1.15 可配置窗口中值滤波状态
 *
 * @param state 滤波状态；NULL 时静默返回
 */
void bm_algo_median_q15_reset(bm_algo_median_q15_state_t *state);

/**
 * @brief 更新 Q1.15 有限窗口中值滤波器
 *
 * @param state 滤波状态
 * @param config 窗口长度，最大 16
 * @param input_q15 本拍输入
 * @return 当前窗口的 Q1.15 中位数；参数无效时原样返回 input_q15
 */
bm_algo_q15_t bm_algo_median_q15_step(bm_algo_median_q15_state_t *state,
                                      const bm_algo_median_q15_config_t *config,
                                      bm_algo_q15_t input_q15);

#define BM_ALGO_MEDIAN_Q31_MAX  16u

typedef struct {
    uint16_t window_size;
} bm_algo_median_q31_config_t;

typedef struct {
    bm_algo_q31_t samples[BM_ALGO_MEDIAN_Q31_MAX];
    uint16_t      count;
    uint16_t      index;
} bm_algo_median_q31_state_t;

/**
 * @brief 清零 Q1.31 可配置窗口中值滤波状态
 *
 * @param state 滤波状态；NULL 时静默返回
 */
void bm_algo_median_q31_reset(bm_algo_median_q31_state_t *state);

/**
 * @brief 更新 Q1.31 有限窗口中值滤波器
 *
 * @param state 滤波状态
 * @param config 窗口长度，最大 16
 * @param input_q31 本拍输入
 * @return 当前窗口的 Q1.31 中位数；参数无效时原样返回 input_q31
 */
bm_algo_q31_t bm_algo_median_q31_step(bm_algo_median_q31_state_t *state,
                                      const bm_algo_median_q31_config_t *config,
                                      bm_algo_q31_t input_q31);

typedef struct {
    bm_algo_q31_t step_v_q31;
    bm_algo_q31_t v_min_q31;
    bm_algo_q31_t v_max_q31;
} bm_algo_mppt_po_q31_config_t;

typedef struct {
    bm_algo_q31_t v_ref_q31;
    bm_algo_q31_t prev_power_q31;
    int           direction;
} bm_algo_mppt_po_q31_state_t;

/**
 * @brief 复位 Q1.31 扰动观察法 MPPT 状态
 *
 * @param state MPPT 状态；NULL 时静默返回
 * @param v_init_q31 初始参考电压
 */
void bm_algo_mppt_po_q31_reset(bm_algo_mppt_po_q31_state_t *state,
                               bm_algo_q31_t v_init_q31);

/**
 * @brief 执行一拍 Q1.31 扰动观察法 MPPT
 *
 * @param state MPPT 状态
 * @param config 步长及参考电压限幅
 * @param voltage_q31 光伏电压
 * @param current_q31 光伏电流
 * @return 限幅后的参考电压；state 或 config 为 NULL 时返回 voltage_q31
 */
bm_algo_q31_t bm_algo_mppt_po_q31_step(bm_algo_mppt_po_q31_state_t *state,
                                       const bm_algo_mppt_po_q31_config_t *config,
                                       bm_algo_q31_t voltage_q31,
                                       bm_algo_q31_t current_q31);

typedef struct {
    bm_algo_q31_t step_v_q31;
    bm_algo_q31_t v_min_q31;
    bm_algo_q31_t v_max_q31;
} bm_algo_mppt_ic_q31_config_t;

typedef struct {
    bm_algo_q31_t v_ref_q31;
    bm_algo_q31_t prev_v_q31;
    bm_algo_q31_t prev_i_q31;
} bm_algo_mppt_ic_q31_state_t;

/**
 * @brief 复位 Q1.31 增量电导法 MPPT 状态
 *
 * @param state MPPT 状态；NULL 时静默返回
 * @param v_init_q31 初始参考电压
 */
void bm_algo_mppt_ic_q31_reset(bm_algo_mppt_ic_q31_state_t *state,
                               bm_algo_q31_t v_init_q31);

/**
 * @brief 执行一拍 Q1.31 增量电导法 MPPT
 *
 * @param state MPPT 状态
 * @param config 步长及参考电压限幅
 * @param voltage_q31 光伏电压
 * @param current_q31 光伏电流
 * @return 限幅后的参考电压；state 或 config 为 NULL 时返回 voltage_q31
 */
bm_algo_q31_t bm_algo_mppt_ic_q31_step(bm_algo_mppt_ic_q31_state_t *state,
                                       const bm_algo_mppt_ic_q31_config_t *config,
                                       bm_algo_q31_t voltage_q31,
                                       bm_algo_q31_t current_q31);

typedef struct {
    bm_algo_q15_t kp_q15;
    bm_algo_q15_t ki_q15;
    bm_algo_q15_t kd_q15;
    bm_algo_q15_t b_q15;
    bm_algo_q15_t out_min;
    bm_algo_q15_t out_max;
    bm_algo_q15_t integrator_min;
    bm_algo_q15_t integrator_max;
    bm_algo_q15_t d_filter_coeff_q15;
} bm_algo_pid2_q15_config_t;

typedef struct {
    bm_algo_q15_t integrator;
    bm_algo_q15_t prev_measurement;
    bm_algo_q15_t d_filtered;
    bm_algo_q15_t output;
} bm_algo_pid2_q15_state_t;

/**
 * @brief 复位 Q1.15 二自由度 PID 状态
 *
 * @param state 控制器状态；NULL 时静默返回
 * @param output 初始输出
 */
void bm_algo_pid2_q15_reset(bm_algo_pid2_q15_state_t *state, bm_algo_q15_t output);

/**
 * @brief 执行一拍 Q1.15 二自由度 PID 控制
 *
 * @param state 控制器状态
 * @param config PID 参数、设定值权重、微分滤波和限幅
 * @param reference_q15 参考值
 * @param measurement_q15 测量值
 * @param dt_q15 Q1.15 时间步长，必须大于 0
 * @return 限幅后的 Q1.15 输出；参数无效时返回 0
 */
bm_algo_q15_t bm_algo_pid2_q15_step(bm_algo_pid2_q15_state_t *state,
                                    const bm_algo_pid2_q15_config_t *config,
                                    bm_algo_q15_t reference_q15,
                                    bm_algo_q15_t measurement_q15,
                                    bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q31_t kp_q31;
    bm_algo_q31_t ki_q31;
    bm_algo_q31_t kd_q31;
    bm_algo_q31_t b_q31;
    bm_algo_q31_t out_min;
    bm_algo_q31_t out_max;
    bm_algo_q31_t integrator_min;
    bm_algo_q31_t integrator_max;
    bm_algo_q31_t d_filter_alpha_q31;
} bm_algo_pid2_q31_config_t;

typedef struct {
    bm_algo_q31_t integrator;
    bm_algo_q31_t prev_measurement;
    bm_algo_q31_t d_filtered;
    bm_algo_q31_t output;
} bm_algo_pid2_q31_state_t;

/**
 * @brief 复位 Q1.31 二自由度 PID 状态
 *
 * @param state 控制器状态；NULL 时静默返回
 * @param output 初始输出
 */
void bm_algo_pid2_q31_reset(bm_algo_pid2_q31_state_t *state, bm_algo_q31_t output);

/**
 * @brief 执行一拍 Q1.31 二自由度 PID 控制
 *
 * @param state 控制器状态
 * @param config PID 参数、设定值权重、微分滤波和限幅
 * @param reference_q31 参考值
 * @param measurement_q31 测量值
 * @param dt_q31 Q1.31 时间步长，必须大于 0
 * @return 限幅后的 Q1.31 输出；参数无效时返回 0
 */
bm_algo_q31_t bm_algo_pid2_q31_step(bm_algo_pid2_q31_state_t *state,
                                    const bm_algo_pid2_q31_config_t *config,
                                    bm_algo_q31_t reference_q31,
                                    bm_algo_q31_t measurement_q31,
                                    bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q31_t min_v_q31;
    bm_algo_q31_t max_v_q31;
    bm_algo_q31_t max_rate_per_s_q31;
} bm_algo_range_monitor_q31_config_t;

typedef struct {
    bm_algo_q31_t prev_q31;
    uint32_t      fault_flags;
} bm_algo_range_monitor_q31_state_t;

/**
 * @brief 复位 Q1.31 范围监控器并清除故障
 *
 * @param state 监控状态；NULL 时静默返回
 * @param v_q31 初始样本
 */
void bm_algo_range_monitor_q31_reset(bm_algo_range_monitor_q31_state_t *state,
                                     bm_algo_q31_t v_q31);

/**
 * @brief 检测 Q1.31 样本的越界、变化率和冻结故障
 *
 * @param state 监控状态
 * @param config 范围与最大变化率配置
 * @param sample_q31 本拍样本
 * @param dt_q31 Q1.31 时间步长
 * @return 本拍累计故障位掩码；state 或 config 为 NULL 时返回 0
 */
uint32_t bm_algo_range_monitor_q31_step(
    bm_algo_range_monitor_q31_state_t *state,
    const bm_algo_range_monitor_q31_config_t *config,
    bm_algo_q31_t sample_q31,
    bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q15_t model_gain_q15;
    uint32_t      delay_steps;
} bm_algo_smith_predictor_q15_config_t;

typedef struct {
    bm_algo_q15_t *u_delay_line_q15;
    uint32_t       line_len;
    uint32_t       delay_steps;
    uint32_t       head;
} bm_algo_smith_predictor_q15_state_t;

/**
 * @brief 绑定并清零 Q1.15 Smith 预估器延迟线
 *
 * @param state 预估器状态
 * @param config 模型增益与延迟拍数
 * @param delay_line_q15 调用方提供的延迟线
 * @param line_len 延迟线元素数
 * @return BM_OK 成功；指针无效、延迟为 0 或容量不足时返回 BM_ERR_INVALID
 */
int bm_algo_smith_predictor_q15_init(
    bm_algo_smith_predictor_q15_state_t *state,
    const bm_algo_smith_predictor_q15_config_t *config,
    bm_algo_q15_t *delay_line_q15,
    uint32_t line_len);

/**
 * @brief 清零 Q1.15 Smith 预估器延迟线与游标
 *
 * @param state 预估器状态；无效时静默返回
 * @param config 延迟配置；无效或与已绑定容量不符时静默返回
 */
void bm_algo_smith_predictor_q15_reset(
    bm_algo_smith_predictor_q15_state_t *state,
    const bm_algo_smith_predictor_q15_config_t *config);

/**
 * @brief 通过浮点桥接执行一拍 Q1.15 Smith 预估
 *
 * @param state 预估器状态
 * @param config 模型增益与延迟拍数
 * @param reference_q15 参考值
 * @param measurement_q15 测量值
 * @param u_controller_q15 控制器输出
 * @return Q1.15 预测误差；状态无效或延迟超过内部 8 拍桥接上限时返回饱和的 reference-measurement
 */
bm_algo_q15_t bm_algo_smith_predictor_q15_step(
    bm_algo_smith_predictor_q15_state_t *state,
    const bm_algo_smith_predictor_q15_config_t *config,
    bm_algo_q15_t reference_q15,
    bm_algo_q15_t measurement_q15,
    bm_algo_q15_t u_controller_q15);

typedef struct {
    bm_algo_q31_t model_gain_q31;
    uint32_t      delay_steps;
} bm_algo_smith_predictor_q31_config_t;

typedef struct {
    bm_algo_q31_t *u_delay_line_q31;
    uint32_t       line_len;
    uint32_t       delay_steps;
    uint32_t       head;
} bm_algo_smith_predictor_q31_state_t;

/**
 * @brief 绑定并清零 Q1.31 Smith 预估器延迟线
 *
 * @param state 预估器状态
 * @param config 模型增益与延迟拍数
 * @param delay_line_q31 调用方提供的延迟线
 * @param line_len 延迟线元素数
 * @return BM_OK 成功；指针无效、延迟为 0 或容量不足时返回 BM_ERR_INVALID
 */
int bm_algo_smith_predictor_q31_init(
    bm_algo_smith_predictor_q31_state_t *state,
    const bm_algo_smith_predictor_q31_config_t *config,
    bm_algo_q31_t *delay_line_q31,
    uint32_t line_len);

/**
 * @brief 清零 Q1.31 Smith 预估器延迟线与游标
 *
 * @param state 预估器状态；无效时静默返回
 * @param config 延迟配置；无效或与已绑定容量不符时静默返回
 */
void bm_algo_smith_predictor_q31_reset(
    bm_algo_smith_predictor_q31_state_t *state,
    const bm_algo_smith_predictor_q31_config_t *config);

/**
 * @brief 通过浮点桥接执行一拍 Q1.31 Smith 预估
 *
 * @param state 预估器状态
 * @param config 模型增益与延迟拍数
 * @param reference_q31 参考值
 * @param measurement_q31 测量值
 * @param u_controller_q31 控制器输出
 * @return Q1.31 预测误差；状态无效或延迟超过内部 8 拍桥接上限时返回饱和的 reference-measurement
 */
bm_algo_q31_t bm_algo_smith_predictor_q31_step(
    bm_algo_smith_predictor_q31_state_t *state,
    const bm_algo_smith_predictor_q31_config_t *config,
    bm_algo_q31_t reference_q31,
    bm_algo_q31_t measurement_q31,
    bm_algo_q31_t u_controller_q31);

typedef struct {
    bm_algo_q15_t nominal_omega_q15;
    bm_algo_q15_t k_sogi_q15;
    bm_algo_q15_t k_pll_q15;
} bm_algo_sogi_pll_q15_config_t;

typedef struct {
    bm_algo_q15_t theta_rad_q15;
    bm_algo_q15_t omega_rad_s_q15;
    float theta_rad;
    float omega_rad_s;
    float v_alpha;
    float v_beta;
    float integrator;
    float d_alpha_prev;  /**< 前一拍 v_alpha 导数缓存（Tustin 梯形积分用，与 float 版对齐） */
    float d_beta_prev;   /**< 前一拍 v_beta  导数缓存（Tustin 梯形积分用，与 float 版对齐） */
} bm_algo_sogi_pll_q15_state_t;

/**
 * @brief 按 Q1.15 配置复位浮点桥接 SOGI-PLL 状态
 *
 * @param state PLL 状态；NULL 时静默返回
 * @param config 标称角频率及 SOGI/PLL 增益；NULL 时静默返回
 */
void bm_algo_sogi_pll_q15_reset(bm_algo_sogi_pll_q15_state_t *state,
                                const bm_algo_sogi_pll_q15_config_t *config);

/**
 * @brief 通过浮点 Tustin 核推进一拍 Q1.15 SOGI-PLL
 *
 * @param state PLL 状态；NULL 时静默返回
 * @param config 标称角频率及 SOGI/PLL 增益；NULL 时静默返回
 * @param v_input_q15 Q1.15 输入电压
 * @param dt_q15 Q1.15 时间步长；非正时静默返回
 */
void bm_algo_sogi_pll_q15_step(bm_algo_sogi_pll_q15_state_t *state,
                               const bm_algo_sogi_pll_q15_config_t *config,
                               bm_algo_q15_t v_input_q15,
                               bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q31_t nominal_omega_q31;
    bm_algo_q31_t k_sogi_q31;
    bm_algo_q31_t k_pll_q31;
} bm_algo_sogi_pll_q31_config_t;

typedef struct {
    bm_algo_q31_t theta_rad_q31;
    bm_algo_q31_t omega_rad_s_q31;
    float theta_rad;
    float omega_rad_s;
    float v_alpha;
    float v_beta;
    float integrator;
    float d_alpha_prev;  /**< 前一拍 v_alpha 导数缓存（Tustin 梯形积分用，与 float 版对齐） */
    float d_beta_prev;   /**< 前一拍 v_beta  导数缓存（Tustin 梯形积分用，与 float 版对齐） */
} bm_algo_sogi_pll_q31_state_t;

/**
 * @brief 按 Q1.31 配置复位浮点桥接 SOGI-PLL 状态
 *
 * @param state PLL 状态；NULL 时静默返回
 * @param config 标称角频率及 SOGI/PLL 增益；NULL 时静默返回
 */
void bm_algo_sogi_pll_q31_reset(bm_algo_sogi_pll_q31_state_t *state,
                                const bm_algo_sogi_pll_q31_config_t *config);

/**
 * @brief 通过浮点 Tustin 核推进一拍 Q1.31 SOGI-PLL
 *
 * @param state PLL 状态；NULL 时静默返回
 * @param config 标称角频率及 SOGI/PLL 增益；NULL 时静默返回
 * @param v_input_q31 Q1.31 输入电压
 * @param dt_q31 Q1.31 时间步长；非正时静默返回
 */
void bm_algo_sogi_pll_q31_step(bm_algo_sogi_pll_q31_state_t *state,
                               const bm_algo_sogi_pll_q31_config_t *config,
                               bm_algo_q31_t v_input_q31,
                               bm_algo_q31_t dt_q31);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_FIXED_H */
