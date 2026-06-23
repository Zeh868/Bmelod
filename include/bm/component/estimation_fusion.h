/**
 * @file estimation_fusion.h
 * @brief 姿态/状态估算融合选择器（互补 / Mahony / EKF-CV）
 *
 * 支持三种融合模式，并提供 bm_exec_ops_t 调度封装。
 * run 回调驱动 IMU 读取→step→publish 全流程。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.3
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            落地 EKF_CV 融合模式；补 SPDX 头
 * 2026-06-23       0.3            zeh            补 exec_ops 声明；完善公共函数 Doxygen
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_ESTIMATION_FUSION_H
#define BM_ESTIMATION_FUSION_H

#include "bm/algorithm/bm_algo_estimator.h"
#include "bm/algorithm/bm_algo_fusion.h"
#include "bm/hybrid/bm_exec.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_EST_FUSION_TEL_VALID   (1u << 0u)
#define BM_EST_FUSION_TEL_NO_IMU  (1u << 1u)
#define BM_EST_FUSION_TEL_STALE   (1u << 2u)

/**
 * @brief 融合模式枚举
 *
 * - BM_EST_FUSION_COMPLEMENTARY：互补滤波，输出 roll/pitch，yaw 固定为 0。
 * - BM_EST_FUSION_MAHONY：Mahony AHRS，输出 roll/pitch/yaw（四元数转欧拉）。
 * - BM_EST_FUSION_EKF_CV：EKF 匀速模型单轴 pitch 跟踪。
 *   使用要点：
 *   (1) config.ekf_cv.q_pos / q_vel 控制过程噪声（建议 0.001～0.1）；
 *   (2) config.ekf_cv.r_pos 控制加速度计测量噪声（建议 0.01～1.0）；
 *   (3) 仅跟踪 pitch 角，roll 与 yaw 输出固定为 0；
 *   (4) 需在 reset 后调用 init，由 validate_config 检查噪声参数合法性。
 */
typedef enum {
    BM_EST_FUSION_COMPLEMENTARY = 0, /**< 互补滤波（roll/pitch） */
    BM_EST_FUSION_MAHONY,            /**< Mahony AHRS（roll/pitch/yaw） */
    BM_EST_FUSION_EKF_CV             /**< EKF 匀速模型单轴 pitch 跟踪 */
} bm_estimation_fusion_mode_t;

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    roll_rad;
    float    pitch_rad;
    float    yaw_rad;
} bm_estimation_fusion_telemetry_t;

typedef int (*bm_estimation_fusion_read_imu_fn)(void *user,
                                                float *gx, float *gy, float *gz,
                                                float *ax, float *ay, float *az);

typedef void (*bm_estimation_fusion_publish_fn)(
    void *user,
    const bm_estimation_fusion_telemetry_t *telemetry);

typedef struct {
    bm_estimation_fusion_read_imu_fn read_imu;
    void                            *read_imu_user;
    bm_estimation_fusion_publish_fn  publish_telemetry;
    void                            *publish_telemetry_user;
} bm_estimation_fusion_resources_t;

typedef struct {
    bm_estimation_fusion_mode_t      mode;
    bm_algo_complementary_config_t   complementary;
    bm_algo_mahony_config_t          mahony;
    bm_algo_ekf_cv_config_t          ekf_cv;
    float                            dt_s;
} bm_estimation_fusion_config_t;

typedef struct {
    bm_algo_complementary_state_t complementary;
    bm_algo_mahony_state_t        mahony;
    bm_algo_ekf_cv_state_t        ekf_cv;
    bm_algo_euler_t               euler;
    uint32_t                      step_count;
    bm_estimation_fusion_telemetry_t telemetry;
} bm_estimation_fusion_state_t;

typedef struct {
    bm_estimation_fusion_config_t    config;
    bm_estimation_fusion_resources_t resources;
    bm_estimation_fusion_state_t     state;
} bm_estimation_fusion_axis_t;

/**
 * @brief 校验融合组件配置参数
 *
 * 检查公共字段（dt_s > 0）及各模式专属约束：
 * EKF_CV 模式需要 q_pos、q_vel、r_pos 均为非负有限值。
 *
 * @param config 指向配置结构体的只读指针
 * @return BM_OK 校验通过；BM_ERR_INVALID 参数非法
 */
int  bm_estimation_fusion_validate_config(
    const bm_estimation_fusion_config_t *config);

/**
 * @brief 初始化融合轴（先 validate 再 reset）
 *
 * @param axis 指向轴实例，config 须已填充
 * @return BM_OK 成功；BM_ERR_INVALID axis 为 NULL 或配置非法
 */
int  bm_estimation_fusion_init(bm_estimation_fusion_axis_t *axis);

/**
 * @brief 复位融合轴内部状态（不修改 config/resources）
 *
 * @param axis 指向轴实例；为 NULL 时静默返回
 */
void bm_estimation_fusion_reset(bm_estimation_fusion_axis_t *axis);

/**
 * @brief 执行一个控制周期的融合计算
 *
 * 从 resources.read_imu 读取 IMU 数据，依据 config.mode 分发到对应算法，
 * 更新 state.euler，并通过 resources.publish_telemetry 上报遥测。
 * read_imu 为 NULL 或读取失败时打对应 status 标志后直接发布上一拍数据。
 *
 * @param axis 指向已初始化的轴实例；为 NULL 时静默返回
 */
void bm_estimation_fusion_step(bm_estimation_fusion_axis_t *axis);

/**
 * @brief exec_ops run 回调：将 instance->state 转发至 bm_estimation_fusion_step
 *
 * @param instance bm_exec_t 实例（state 指向 bm_estimation_fusion_axis_t）
 */
void bm_estimation_fusion_exec_run(const bm_exec_t *instance);

/**
 * @brief exec_ops init 回调：校验配置并完成初始化
 *
 * @param instance bm_exec_t 实例
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法或指针为 NULL
 */
int  bm_estimation_fusion_exec_init(const bm_exec_t *instance);

/**
 * @brief exec_ops start 回调（当前无需额外操作）
 *
 * @param instance bm_exec_t 实例
 * @return BM_OK
 */
int  bm_estimation_fusion_exec_start(const bm_exec_t *instance);

/**
 * @brief exec_ops safe_stop 回调：清零欧拉角输出
 *
 * @param instance bm_exec_t 实例
 */
void bm_estimation_fusion_exec_safe_stop(const bm_exec_t *instance);

/** @brief estimation_fusion exec_ops 表，可直接赋给 bm_exec_t.ops */
extern const bm_exec_ops_t bm_estimation_fusion_exec_ops;

#ifdef __cplusplus
}
#endif

#endif /* BM_ESTIMATION_FUSION_H */
