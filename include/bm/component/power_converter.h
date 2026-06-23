/**
 * @file power_converter.h
 * @brief Buck 峰值电流模式领域组件（电流参考斜坡 + 电流 PI）
 *
 * 提供单轴 Buck 变换器控制逻辑：外部指令经斜坡限速生成电流参考，
 * PI 调节电流误差输出占空比；支持故障锁存与 exec_ops 框架集成。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            补全 Doxygen 中文注释；添加 SPDX 头
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_POWER_CONVERTER_H
#define BM_POWER_CONVERTER_H

#include "bm/algorithm/bm_algo_control.h"
#include "bm/algorithm/bm_algo_profile.h"
#include "bm/hybrid/bm_exec.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 指令 status 位：变换器使能 */
#define BM_PWR_CONV_CMD_ENABLED  (1u << 0u)
/** @brief 指令 status 位：外部故障请求（立即锁存） */
#define BM_PWR_CONV_CMD_FAULT    (1u << 1u)

/** @brief 遥测 status 位：遥测数据有效 */
#define BM_PWR_CONV_TEL_VALID    (1u << 0u)
/** @brief 遥测 status 位：占空比已饱和（保留，当前未置位） */
#define BM_PWR_CONV_TEL_SAT      (1u << 1u)
/** @brief 遥测 status 位：故障已锁存 */
#define BM_PWR_CONV_TEL_FAULT    (1u << 2u)

/**
 * @brief 功率变换器遥测快照
 */
typedef struct {
    uint32_t sequence; /**< 单调递增序列号（每次正常完成电流步 +1） */
    uint32_t status;   /**< 状态位掩码，见 BM_PWR_CONV_TEL_* */
    float    i_set_a;  /**< 外部设定电流（A） */
    float    i_out_a;  /**< 实测输出电流（A） */
    float    i_ref_a;  /**< 经斜坡处理后的电流参考（A） */
    float    duty;     /**< 当前输出占空比，范围 [duty_min, duty_max] */
} bm_pwr_conv_telemetry_t;

/**
 * @brief 功率变换器外部指令
 */
typedef struct {
    uint32_t sequence; /**< 指令序列号（由上层维护） */
    uint32_t status;   /**< 指令状态位掩码，见 BM_PWR_CONV_CMD_* */
    float    i_set_a;  /**< 目标电流设定值（A） */
} bm_pwr_conv_cmd_t;

/**
 * @brief 读取实际输出电流回调函数原型
 *
 * @param user     用户上下文指针
 * @param i_out_a  输出：实际电流（A）
 * @return 0 成功；非零表示传感器故障（将触发故障锁存）
 */
typedef int (*bm_pwr_conv_read_current_fn)(void *user, float *i_out_a);

/**
 * @brief 写入 PWM 占空比回调函数原型
 *
 * @param user 用户上下文指针
 * @param duty 占空比，范围 [duty_min, duty_max]
 * @return 0 成功；非零表示执行器故障（将触发故障锁存）
 */
typedef int (*bm_pwr_conv_write_duty_fn)(void *user, float duty);

/**
 * @brief 读取外部指令回调函数原型
 *
 * @param user    用户上下文指针
 * @param command 输出：外部指令结构体
 * @return 0 成功；非零表示无可用指令（本帧跳过同步）
 */
typedef int (*bm_pwr_conv_read_command_fn)(void *user,
                                           bm_pwr_conv_cmd_t *command);

/**
 * @brief 发布遥测回调函数原型
 *
 * @param user      用户上下文指针
 * @param telemetry 当前遥测快照（const，生命周期仅在回调内有效）
 */
typedef void (*bm_pwr_conv_publish_telemetry_fn)(
    void *user,
    const bm_pwr_conv_telemetry_t *telemetry);

/**
 * @brief 功率变换器轴外部资源绑定
 */
typedef struct {
    bm_pwr_conv_read_current_fn       read_current;           /**< 电流传感器读回调，NULL 时跳过 */
    void                             *read_current_user;      /**< read_current 用户上下文 */
    bm_pwr_conv_write_duty_fn         write_duty;             /**< 占空比写回调，NULL 时跳过 */
    void                             *write_duty_user;        /**< write_duty 用户上下文 */
    bm_pwr_conv_read_command_fn       read_command;           /**< 指令读回调，NULL 时不同步 */
    void                             *read_command_user;      /**< read_command 用户上下文 */
    bm_pwr_conv_publish_telemetry_fn  publish_telemetry;      /**< 遥测发布回调，NULL 时不发布 */
    void                             *publish_telemetry_user; /**< publish_telemetry 用户上下文 */
} bm_power_converter_resources_t;

/**
 * @brief 功率变换器轴配置
 */
typedef struct {
    bm_algo_pi_config_t    pi_current;    /**< 电流 PI 控制器配置 */
    bm_algo_ramp_config_t  i_ramp;        /**< 电流参考斜坡配置（变化率，A/s） */
    float                  duty_min;      /**< 占空比下限 */
    float                  duty_max;      /**< 占空比上限，须 > duty_min */
    float                  current_dt_s;  /**< 电流快环控制周期（秒），须 > 0 */
} bm_power_converter_config_t;

/**
 * @brief 功率变换器轴运行状态
 */
typedef struct {
    bm_algo_pi_state_t   pi_current;    /**< 电流 PI 运行状态 */
    bm_algo_ramp_state_t i_ramp;        /**< 电流参考斜坡运行状态 */
    float                i_ref_a;       /**< 当前斜坡输出电流参考（A） */
    float                duty;          /**< 当前输出占空比 */
    bm_pwr_conv_cmd_t    cmd;           /**< 最新外部指令缓存 */
    bm_pwr_conv_telemetry_t telemetry;  /**< 最新遥测快照 */
    int                  fault_latched; /**< 非零表示故障已锁存 */
    uint32_t             current_loops; /**< 正常完成电流步的累计次数 */
} bm_power_converter_state_t;

/**
 * @brief 功率变换器轴聚合对象
 */
typedef struct {
    bm_power_converter_config_t    config;    /**< 轴配置（用户填写） */
    bm_power_converter_resources_t resources; /**< 外部资源绑定 */
    bm_power_converter_state_t     state;     /**< 运行状态（由组件维护） */
} bm_power_converter_axis_t;

/**
 * @brief 校验功率变换器配置合法性
 *
 * @param config 配置结构体指针（const），NULL 时返回 BM_ERR_INVALID
 * @return BM_OK 合法；BM_ERR_INVALID 任一字段不合法
 */
int bm_power_converter_validate_config(const bm_power_converter_config_t *config);

/**
 * @brief 复位功率变换器轴运行状态（duty = duty_min，无故障）
 *
 * @param axis 轴实例指针，NULL 时静默返回
 */
void bm_power_converter_reset(bm_power_converter_axis_t *axis);

/**
 * @brief 清除故障锁存，允许重新运行
 *
 * axis 为 NULL 或当前未锁存时静默返回。
 *
 * @param axis 轴实例指针，NULL 时静默返回
 */
void bm_power_converter_clear_fault(bm_power_converter_axis_t *axis);

/**
 * @brief 应用外部指令到轴状态
 *
 * @param axis 轴实例指针，NULL 时静默返回
 * @param cmd  外部指令（const），NULL 时静默返回
 */
void bm_power_converter_apply_command(bm_power_converter_axis_t *axis,
                                      const bm_pwr_conv_cmd_t *cmd);

/**
 * @brief 电流快环单步更新（电流参考斜坡 + 电流 PI → 占空比）
 *
 * @param axis 轴实例指针，NULL 时静默返回
 */
void bm_power_converter_current_step(bm_power_converter_axis_t *axis);

/**
 * @brief exec_ops 快环包装（供 bm_exec 框架调用）
 *
 * @param instance exec 实例指针
 */
void bm_power_converter_exec_current(const bm_exec_t *instance);

/**
 * @brief exec_ops init 包装：校验配置并复位轴
 *
 * @param instance exec 实例指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数/配置非法
 */
int bm_power_converter_exec_init(const bm_exec_t *instance);

/**
 * @brief exec_ops start 包装（无启动动作，直接返回 BM_OK）
 *
 * @param instance exec 实例指针
 * @return 始终返回 BM_OK
 */
int bm_power_converter_exec_start(const bm_exec_t *instance);

/**
 * @brief exec_ops safe_stop 包装：清零电流参考并输出 duty_min
 *
 * @param instance exec 实例指针
 */
void bm_power_converter_exec_safe_stop(const bm_exec_t *instance);

/** @brief 功率变换器 exec_ops 表（电流快环） */
extern const bm_exec_ops_t bm_power_converter_exec_ops;

#ifdef __cplusplus
}
#endif

#endif /* BM_POWER_CONVERTER_H */
