/**
 * @file solar_control.h
 * @brief 光伏 MPPT 编排与限功率骨架
 *
 * 封装 P&O/增量电导 MPPT 与功率限额降额，输出工作点参考。
 * 提供 bm_exec_ops_t 接口，可直接挂入框架调度器。
 * 使能门控仅在选择命令通道模型时生效：绑定 `read_command`（非 NULL）后
 * 默认未使能，须经 apply_command/回调置 ENABLED 后 step 才跑环；
 * `read_command` 为 NULL（未接命令通道）时保持恒使能 legacy 语义
 * （2026-08-01 前行为），step 直接跑环。
 * 故障锁存（fault_latched / CMD_FAULT）与命令通道无关、无条件生效，
 * 仅能经 reset 清除；清除命令 FAULT 位不会自动解锁。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.6
 * @date 2026-08-02
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            补 exec_ops 封装声明；validate_config 字段校验
 * 2026-08-01       0.3            zeh            对齐 power_control：CMD_ENABLED/FAULT 状态机
 * 2026-08-01       0.4            zeh            exec_safe_stop 复位 MPPT；reset NULL 契约对齐
 * 2026-08-01       0.5            zeh            read_iv 未绑定按零值继续；文档化故障仅 reset 清除
 * 2026-08-02       0.6            zeh            使能门控改绑 read_command 是否接入：NULL=恒使能
 *                                                legacy（修复 0.3 对未接命令通道消费方的静默零
 *                                                输出破坏），非 NULL=命令驱动使能（默认未使能）
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_SOLAR_CONTROL_H
#define BM_SOLAR_CONTROL_H

#include "bm/algorithm/bm_algo_power.h"
#include "bm/hybrid/bm_exec.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 命令状态位：使能 MPPT 输出（仅在绑定 read_command 后有门控意义） */
#define BM_SOLAR_CTRL_CMD_ENABLED  (1u << 0u)
/** @brief 命令状态位：外部故障锁定 */
#define BM_SOLAR_CTRL_CMD_FAULT    (1u << 1u)

/** @brief 遥测状态位：数据有效 */
#define BM_SOLAR_CTRL_TEL_VALID     (1u << 0u)
/** @brief 遥测状态位：已限功率降额 */
#define BM_SOLAR_CTRL_TEL_LIMITED   (1u << 1u)
/** @brief 遥测状态位：采样陈旧（历史兼容；读失败现同时锁故障） */
#define BM_SOLAR_CTRL_TEL_STALE     (1u << 2u)
/** @brief 遥测状态位：当前帧处于故障态 */
#define BM_SOLAR_CTRL_TEL_FAULT     (1u << 3u)

typedef enum {
    BM_SOLAR_MPPT_PO = 0,
    BM_SOLAR_MPPT_IC
} bm_solar_mppt_mode_t;

/**
 * @brief 光伏控制命令
 */
typedef struct {
    uint32_t sequence; /**< 命令序列号 */
    uint32_t status;   /**< 命令状态位（BM_SOLAR_CTRL_CMD_* 位域） */
} bm_solar_ctrl_cmd_t;

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    voltage_v;
    float    current_a;
    float    power_w;
    float    v_ref_v;
} bm_solar_control_telemetry_t;

typedef int (*bm_solar_read_iv_fn)(void *user,
                                   float *voltage_v,
                                   float *current_a);

typedef int (*bm_solar_write_vref_fn)(void *user, float v_ref_v);

typedef void (*bm_solar_publish_fn)(void *user,
                                    const bm_solar_control_telemetry_t *telemetry);

/**
 * @brief 命令读取回调函数类型
 *
 * @param user    用户上下文指针
 * @param command 输出：最新命令
 * @return 0 成功；非零 无新命令
 */
typedef int (*bm_solar_read_command_fn)(void *user,
                                        bm_solar_ctrl_cmd_t *command);

typedef struct {
    bm_solar_read_iv_fn       read_iv;           /**< 可为 NULL；未绑定按零值继续 */
    void                     *read_iv_user;
    bm_solar_write_vref_fn    write_vref;
    void                     *write_vref_user;
    bm_solar_publish_fn       publish_telemetry;
    void                     *publish_telemetry_user;
    bm_solar_read_command_fn  read_command;      /**< 可为 NULL；NULL=未接命令通道（恒使能 legacy），非 NULL=命令驱动使能（默认未使能，须 CMD_ENABLED） */
    void                     *read_command_user;
} bm_solar_control_resources_t;

typedef struct {
    bm_solar_mppt_mode_t       mppt_mode;
    bm_algo_mppt_po_config_t   mppt_po;
    bm_algo_mppt_ic_config_t   mppt_ic;
    float                      power_limit_w;
    float                      v_init_v;
} bm_solar_control_config_t;

typedef struct {
    bm_algo_mppt_po_state_t po;
    bm_algo_mppt_ic_state_t ic;
    float v_ref_v;
    float last_power_w;
    uint32_t step_count;
    bm_solar_ctrl_cmd_t cmd;           /**< 最新控制命令 */
    int fault_latched;                 /**< 非零：故障已锁存；仅 reset 可清除 */
    bm_solar_control_telemetry_t telemetry;
} bm_solar_control_state_t;

typedef struct {
    bm_solar_control_config_t    config;
    bm_solar_control_resources_t resources;
    bm_solar_control_state_t     state;
} bm_solar_control_axis_t;

/**
 * @brief 校验配置合法性
 *
 * 检查 v_init_v、step_v、power_limit_w 及 MPPT 参数边界。
 *
 * @param config 配置指针（不可为 NULL）
 * @return BM_OK 合法；BM_ERR_INVALID 非法
 */
int  bm_solar_control_validate_config(const bm_solar_control_config_t *config);

/**
 * @brief 初始化控制轴（校验 + 复位）
 *
 * @param axis 控制轴指针（不可为 NULL）
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法
 */
int  bm_solar_control_init(bm_solar_control_axis_t *axis);

/**
 * @brief 复位所有运行状态
 *
 * 清零输出与 MPPT 状态，清除故障锁存与命令。绑定 read_command 时复位
 * 后即回到未使能态（须重新置 ENABLED）；未接命令通道（read_command
 * 为 NULL）时无门控，复位后 step 直接跑环。
 *
 * @param axis 控制轴指针；NULL 时静默返回
 */
void bm_solar_control_reset(bm_solar_control_axis_t *axis);

/**
 * @brief 应用外部控制命令（使能/故障）
 *
 * 若命令携带 FAULT 位则立即锁存故障并将 v_ref 置零。
 *
 * @param axis 实例指针
 * @param cmd  待应用的命令，不可为 NULL
 */
void bm_solar_control_apply_command(bm_solar_control_axis_t *axis,
                                    const bm_solar_ctrl_cmd_t *cmd);

/**
 * @brief 执行一拍 MPPT 步进并处理功率限额降额
 *
 * 先 sync_command；故障锁存时（无条件），或绑定了 read_command 但未
 * ENABLED 时，停止 MPPT 并复位（后者 log-once 告警）；未接命令通道
 * （read_command 为 NULL）时无使能门控，直接跑环。
 * read_iv 未绑定则按零值继续；读失败时锁存故障并发布 FAULT/STALE 遥测。
 * 故障锁存仅能经 reset 清除。
 *
 * @param axis 控制轴指针；NULL 时静默返回
 */
void bm_solar_control_step(bm_solar_control_axis_t *axis);

/**
 * @brief exec_ops 调度入口（instance->state 转发至 step）
 *
 * @param instance bm_exec_t 实例指针（state 字段须指向 bm_solar_control_axis_t）
 */
void bm_solar_control_exec_run(const bm_exec_t *instance);

/**
 * @brief exec_ops init 回调：校验配置并复位状态
 *
 * @param instance bm_exec_t 实例指针
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法或指针为 NULL
 */
int  bm_solar_control_exec_init(const bm_exec_t *instance);

/**
 * @brief exec_ops start 回调（当前无需额外操作）
 *
 * @param instance bm_exec_t 实例指针
 * @return BM_OK
 */
int  bm_solar_control_exec_start(const bm_exec_t *instance);

/**
 * @brief exec_ops safe_stop 回调：清零 v_ref 并写入硬件
 *
 * @param instance bm_exec_t 实例指针
 */
void bm_solar_control_exec_safe_stop(const bm_exec_t *instance);

/** @brief solar_control exec_ops 表，可直接赋给 bm_exec_t.ops */
extern const bm_exec_ops_t bm_solar_control_exec_ops;

#ifdef __cplusplus
}
#endif

#endif /* BM_SOLAR_CONTROL_H */
