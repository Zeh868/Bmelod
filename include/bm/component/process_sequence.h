/**
 * @file process_sequence.h
 * @brief 简化 IEC 定时器与顺序状态机（E1 骨架）
 *
 * 提供 TON/TOF 计数器与多步顺序联锁，支持步超时与条件回调。
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
 * 2026-06-23       0.2            zeh            补 SPDX 与函数级 Doxygen
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_PROCESS_SEQUENCE_H
#define BM_PROCESS_SEQUENCE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 顺序状态机最大步数 */
#define BM_PROCESS_SEQ_MAX_STEPS 8u

/**
 * @brief TON（接通延时）定时器状态
 *
 * input 持续为真超过 preset_ticks 个 tick 后 output 置 1；
 * input 变为假则立即复位 elapsed_ticks 并清零 output。
 */
typedef struct {
    uint32_t preset_ticks;  /**< 延时触发门限（tick 数） */
    uint32_t elapsed_ticks; /**< 已计 tick 数 */
    int      input;         /**< 当前输入状态 */
    int      output;        /**< 当前输出状态（0 或 1） */
} bm_process_ton_state_t;

/**
 * @brief TOF（断开延时）定时器状态
 *
 * input 为真时 output 立即置 1；input 变为假后继续保持 output=1
 * 直到 elapsed_ticks 超过 preset_ticks，随后才清零 output。
 */
typedef struct {
    uint32_t preset_ticks;  /**< 延时释放门限（tick 数） */
    uint32_t elapsed_ticks; /**< 已计 tick 数（自 input 变假起） */
    int      input;         /**< 当前输入状态 */
    int      output;        /**< 当前输出状态（0 或 1） */
} bm_process_tof_state_t;

/**
 * @brief 单步配置
 */
typedef struct {
    float timeout_s; /**< 步超时时间（s）；≤ 0 表示无超时（永久等待联锁） */
} bm_process_sequence_step_config_t;

/**
 * @brief 联锁回调函数类型
 *
 * 由调用方在每步执行时提供；返回非零表示联锁条件满足（允许推进到下一步）。
 *
 * @param user       用户上下文指针
 * @param step_index 当前步索引（0-based）
 * @return 非零：联锁 OK；0：联锁未满足（当前步继续等待）
 */
typedef int (*bm_process_sequence_interlock_fn)(void *user, uint32_t step_index);

/**
 * @brief 顺序状态机静态配置
 */
typedef struct {
    uint32_t                          step_count;                          /**< 实际步数，须 > 0 且 ≤ BM_PROCESS_SEQ_MAX_STEPS */
    bm_process_sequence_step_config_t steps[BM_PROCESS_SEQ_MAX_STEPS];    /**< 各步配置数组 */
    float                             dt_s;                                /**< 步进调用周期（s），须 > 0 */
} bm_process_sequence_config_t;

/**
 * @brief 顺序状态机运行时状态
 */
typedef struct {
    uint32_t current_step;    /**< 当前执行步索引（0-based） */
    float    step_elapsed_s;  /**< 当前步已耗时（s） */
    int      running;         /**< 非零：序列运行中 */
    int      done;            /**< 非零：所有步执行完毕 */
    bm_process_ton_state_t ton; /**< 内置 TON 定时器（供步逻辑复用） */
    bm_process_tof_state_t tof; /**< 内置 TOF 定时器（供步逻辑复用） */
} bm_process_sequence_state_t;

/**
 * @brief 顺序状态机完整实例（配置 + 状态）
 */
typedef struct {
    bm_process_sequence_config_t config; /**< 静态配置，初始化前填写 */
    bm_process_sequence_state_t  state;  /**< 运行时状态，由 API 维护 */
} bm_process_sequence_axis_t;

/**
 * @brief 复位 TON 定时器状态
 *
 * @param state        TON 状态指针；为 NULL 时静默返回
 * @param preset_ticks 延时门限（tick 数）
 */
void bm_process_ton_reset(bm_process_ton_state_t *state, uint32_t preset_ticks);

/**
 * @brief 执行一步 TON（接通延时）计算
 *
 * input 持续为真超过 preset_ticks 个 tick 后 output 置 1；
 * input 为假时立即复位计数并清零 output。
 *
 * @param state TON 状态指针；为 NULL 时返回 0
 * @param input 当前输入（非零为真）
 * @return 当前 output 值（0 或 1）
 */
int  bm_process_ton_step(bm_process_ton_state_t *state, int input);

/**
 * @brief 复位 TOF 定时器状态
 *
 * @param state        TOF 状态指针；为 NULL 时静默返回
 * @param preset_ticks 延时门限（tick 数）
 */
void bm_process_tof_reset(bm_process_tof_state_t *state, uint32_t preset_ticks);

/**
 * @brief 执行一步 TOF（断开延时）计算
 *
 * input 为真时 output 立即置 1；input 变为假后继续保持 output=1
 * 直到延时门限超出后才清零 output。
 *
 * @param state TOF 状态指针；为 NULL 时返回 0
 * @param input 当前输入（非零为真）
 * @return 当前 output 值（0 或 1）
 */
int  bm_process_tof_step(bm_process_tof_state_t *state, int input);

/**
 * @brief 校验顺序状态机配置合法性
 *
 * @param config 指向待校验的配置结构体，不可为 NULL
 * @return BM_OK 合法；BM_ERR_INVALID 参数越界或指针为空
 */
int  bm_process_sequence_validate_config(const bm_process_sequence_config_t *config);

/**
 * @brief 复位顺序状态机（步索引归零，running/done 清零）
 *
 * @param axis 实例指针；为 NULL 时静默返回
 */
void bm_process_sequence_reset(bm_process_sequence_axis_t *axis);

/**
 * @brief 启动顺序状态机（从第 0 步开始运行）
 *
 * 清零步索引与计时器，置 running=1，done=0。
 * 若 axis 为 NULL 则静默返回。
 *
 * @param axis 实例指针
 */
void bm_process_sequence_start(bm_process_sequence_axis_t *axis);

/**
 * @brief 执行一步顺序状态机推进
 *
 * 若 running=0 或 done=1 则静默返回。
 * 累加步内计时；若联锁满足且（超时有效时）步内计时达到 timeout_s，
 * 则推进到下一步；完成所有步后置 done=1, running=0。
 *
 * @param axis           实例指针；为 NULL 时静默返回
 * @param interlock      联锁回调，可为 NULL（NULL 时视为始终满足）
 * @param interlock_user 联锁回调用户上下文
 */
void bm_process_sequence_step(bm_process_sequence_axis_t *axis,
                              bm_process_sequence_interlock_fn interlock,
                              void *interlock_user);

#ifdef __cplusplus
}
#endif

#endif /* BM_PROCESS_SEQUENCE_H */
