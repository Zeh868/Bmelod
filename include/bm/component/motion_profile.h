/**
 * @file motion_profile.h
 * @brief 单轴运动轨迹规划（梯形或 S 曲线）
 *
 * 接收目标位置命令，按 jerk/速度/加速度约束输出位置与速度，
 * 并提供 bm_exec_ops_t 调度封装以接入 bm_exec 生命周期管理。
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
 * 2026-06-23       0.2            zeh            补 exec_ops 声明；NULL 保护（goto/step）；Doxygen；SPDX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_MOTION_PROFILE_H
#define BM_MOTION_PROFILE_H

#include "bm/algorithm/bm_algo_profile.h"
#include "bm/hybrid/bm_exec.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BM_MOTION_PROFILE_TRAP = 0,
    BM_MOTION_PROFILE_SCURVE = 1
} bm_motion_profile_type_t;

typedef struct {
    bm_motion_profile_type_t type;
    float                    jerk;
    float                    vmax;
    float                    amax;
    float                    dt_s;
} bm_motion_profile_config_t;

typedef struct {
    bm_algo_trapezoid_state_t trapezoid;
    bm_algo_scurve_state_t    scurve;
    float                     target_pos;
    int                       active;
    uint32_t                  step_count;
} bm_motion_profile_state_t;

typedef struct {
    float position;
    float velocity;
    int   done;
} bm_motion_profile_output_t;

typedef struct {
    bm_motion_profile_config_t config;
    bm_motion_profile_state_t  state;
} bm_motion_profile_axis_t;

/**
 * @brief 校验运动规划配置合法性
 *
 * 检查 dt_s > 0、vmax > 0、amax > 0；S 曲线模式额外要求 jerk > 0。
 *
 * @param config 配置结构体只读指针，NULL 时返回 BM_ERR_INVALID
 * @return BM_OK 合法；BM_ERR_INVALID 任一字段非法
 */
int  bm_motion_profile_validate_config(const bm_motion_profile_config_t *config);

/**
 * @brief 复位运动规划轴至指定位置（速度/加速度清零）
 *
 * @param axis     轴实例指针；为 NULL 时静默返回
 * @param position 初始位置（工程单位）
 */
void bm_motion_profile_reset(bm_motion_profile_axis_t *axis, float position);

/**
 * @brief 设定运动目标位置并启动轨迹规划
 *
 * 配置非法时静默返回，不改变轴状态。
 *
 * @param axis     轴实例指针；为 NULL 或配置非法时静默返回
 * @param position 目标位置（工程单位）
 */
void bm_motion_profile_goto(bm_motion_profile_axis_t *axis, float position);

/**
 * @brief 执行一个控制周期的轨迹规划步进
 *
 * 按当前轮廓类型（梯形或 S 曲线）推进内部状态，将位置/速度写入 @p out。
 * 达到目标时 out->done = 1，并清除 active 标志。
 * axis/out 为 NULL 或配置非法时静默返回。
 *
 * @param axis 指向已初始化的轴实例；为 NULL 时静默返回
 * @param out  输出缓冲区（位置、速度、完成标志）；为 NULL 时静默返回
 */
void bm_motion_profile_step(bm_motion_profile_axis_t *axis,
                            bm_motion_profile_output_t *out);

/**
 * @brief exec_ops run 回调：将 instance->state 转发至 bm_motion_profile_step
 *
 * out 缓冲区在内部分配，步进结果由调用方直接读取 axis->state。
 *
 * @param instance bm_exec_t 实例（state 指向 bm_motion_profile_axis_t）
 */
void bm_motion_profile_exec_run(const bm_exec_t *instance);

/**
 * @brief exec_ops init 回调：校验配置并 reset 至 position=0
 *
 * @param instance bm_exec_t 实例
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法或指针为 NULL
 */
int  bm_motion_profile_exec_init(const bm_exec_t *instance);

/**
 * @brief exec_ops start 回调（当前无需额外操作）
 *
 * @param instance bm_exec_t 实例
 * @return BM_OK
 */
int  bm_motion_profile_exec_start(const bm_exec_t *instance);

/**
 * @brief exec_ops safe_stop 回调：清除运动激活标志
 *
 * @param instance bm_exec_t 实例
 */
void bm_motion_profile_exec_safe_stop(const bm_exec_t *instance);

/** @brief motion_profile exec_ops 表，可直接赋给 bm_exec_t.ops */
extern const bm_exec_ops_t bm_motion_profile_exec_ops;

#ifdef __cplusplus
}
#endif

#endif /* BM_MOTION_PROFILE_H */
