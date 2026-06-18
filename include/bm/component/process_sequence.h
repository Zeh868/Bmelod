/**
 * @file process_sequence.h
 * @brief 简化 IEC 定时器与顺序状态机（E1 骨架）
 *
 * 提供 TON/TOF 计数器与多步顺序联锁，支持步超时与条件回调。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始骨架
 */
#ifndef BM_PROCESS_SEQUENCE_H
#define BM_PROCESS_SEQUENCE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_PROCESS_SEQ_MAX_STEPS 8u

typedef struct {
    uint32_t preset_ticks;
    uint32_t elapsed_ticks;
    int      input;
    int      output;
} bm_process_ton_state_t;

typedef struct {
    uint32_t preset_ticks;
    uint32_t elapsed_ticks;
    int      input;
    int      output;
} bm_process_tof_state_t;

typedef struct {
    float timeout_s;
} bm_process_sequence_step_config_t;

typedef int (*bm_process_sequence_interlock_fn)(void *user, uint32_t step_index);

typedef struct {
    uint32_t                          step_count;
    bm_process_sequence_step_config_t steps[BM_PROCESS_SEQ_MAX_STEPS];
    float                             dt_s;
} bm_process_sequence_config_t;

typedef struct {
    uint32_t current_step;
    float    step_elapsed_s;
    int      running;
    int      done;
    bm_process_ton_state_t ton;
    bm_process_tof_state_t tof;
} bm_process_sequence_state_t;

typedef struct {
    bm_process_sequence_config_t config;
    bm_process_sequence_state_t  state;
} bm_process_sequence_axis_t;

void bm_process_ton_reset(bm_process_ton_state_t *state, uint32_t preset_ticks);
int  bm_process_ton_step(bm_process_ton_state_t *state, int input);

void bm_process_tof_reset(bm_process_tof_state_t *state, uint32_t preset_ticks);
int  bm_process_tof_step(bm_process_tof_state_t *state, int input);

int  bm_process_sequence_validate_config(const bm_process_sequence_config_t *config);

void bm_process_sequence_reset(bm_process_sequence_axis_t *axis);

void bm_process_sequence_start(bm_process_sequence_axis_t *axis);

void bm_process_sequence_step(bm_process_sequence_axis_t *axis,
                              bm_process_sequence_interlock_fn interlock,
                              void *interlock_user);

#ifdef __cplusplus
}
#endif

#endif /* BM_PROCESS_SEQUENCE_H */
