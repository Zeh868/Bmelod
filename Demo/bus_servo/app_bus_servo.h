/**
 * @file app_bus_servo.h
 * @brief bus_servo 示例：bm_bus 三 mode 伺服控制共享接口
 *
 * 将 closed_loop_servo 的命令/遥测通讯从 bm_snapshot 迁移至 bm_bus 三种模式：
 *   - QUEUE ：指令通道（监督→控制环，保序不丢）
 *   - LATEST：轴最新状态（控制环→监督，覆盖最新）
 *   - SIGNAL：遥测流（控制环→两个独立消费者）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-25
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-25       1.0            zeh            初稿，基于 closed_loop_servo 移植
 *
 */
#ifndef APP_BUS_SERVO_H
#define APP_BUS_SERVO_H

#include "bm/core/bm_bus.h"
#include "bm_exec.h"

#include <stdint.h>

/* =========================================================================
 * 监督层事件 ID
 * ========================================================================= */

/** 监督层事件：使能伺服并发布默认设定值 */
#define EVENT_BUS_SERVO_ENABLE   1u
/** 监督层事件：更新速度设定值 */
#define EVENT_BUS_SERVO_SETPOINT 2u
/** 监督层事件：请求故障停机 */
#define EVENT_BUS_SERVO_FAULT    3u
/** 监督层事件：轮询遥测（ticker 触发） */
#define EVENT_BUS_SERVO_POLL     4u

/* =========================================================================
 * 常量
 * ========================================================================= */

/** 默认速度设定值（rad/s），与 native_sim plant 参数匹配 */
#define BUS_SERVO_SETPOINT_RAD_S  2.0f

/* =========================================================================
 * 命令状态位
 * ========================================================================= */

/** 命令：轴已使能 */
#define BUS_SERVO_CMD_STATUS_ENABLED  (1u << 0u)
/** 命令：故障锁存 */
#define BUS_SERVO_CMD_STATUS_FAULT    (1u << 1u)

/* =========================================================================
 * 遥测状态位
 * ========================================================================= */

/** 遥测：本周期测量有效 */
#define BUS_SERVO_TEL_STATUS_VALID    (1u << 0u)
/** 遥测：PI 输出饱和 */
#define BUS_SERVO_TEL_STATUS_SAT      (1u << 1u)
/** 遥测：故障态 */
#define BUS_SERVO_TEL_STATUS_FAULT    (1u << 2u)

/* =========================================================================
 * 数据类型定义
 * ========================================================================= */

/**
 * @brief 伺服指令（QUEUE bus 元素类型）
 *
 * 监督层写入，控制环消费；保序不丢。
 */
typedef struct {
    uint32_t sequence;                /**< 递增序号，用于检测更新 */
    uint32_t status;                  /**< BUS_SERVO_CMD_STATUS_* 位掩码 */
    float    velocity_setpoint_rad_s; /**< 速度设定（rad/s） */
} bus_servo_command_t;

/**
 * @brief 伺服状态（LATEST bus 元素类型）
 *
 * 控制环每周期写入最新状态；监督层读最新值。
 * 三缓冲覆盖式，无需保序，始终读到最新。
 */
typedef struct {
    uint32_t loop_count;              /**< 控制环执行次数 */
    float    velocity_meas_rad_s;     /**< 当前测量速度 */
    float    velocity_ref_rad_s;      /**< ramp 输出参考速度 */
    float    duty;                    /**< PI 输出占空比 [0,1] */
    int      fault_latched;           /**< 故障锁存标志 */
} bus_servo_state_t;

/**
 * @brief 伺服遥测（SIGNAL bus 元素类型）
 *
 * 控制环每周期发布；两个独立消费者（导出 + 监控）各自独立追赶。
 */
typedef struct {
    uint32_t sequence;                /**< 递增序号 */
    uint32_t status;                  /**< BUS_SERVO_TEL_STATUS_* 位掩码 */
    float    velocity_meas_rad_s;     /**< ADC 反馈速度 */
    float    velocity_ref_rad_s;      /**< ramp 输出参考 */
    float    duty;                    /**< PI 输出占空比 [0,1] */
} bus_servo_telemetry_t;

/* =========================================================================
 * bm_exec 速度环运行时状态
 * ========================================================================= */

/**
 * @brief 速度环运行时内部状态（bm_exec state 指针指向此结构体）
 */
typedef struct {
    uint32_t loop_count;              /**< 控制环执行总次数 */
    float    velocity_meas_rad_s;     /**< 最近一次测量速度 */
    float    velocity_ref_rad_s;      /**< 最近一次 ramp 参考 */
    float    duty;                    /**< 最近一次占空比 */
    uint32_t fault_count;             /**< 故障触发次数 */
    int      fault_latched;           /**< 故障锁存标志 */
    uint32_t cmd_consumed;            /**< 从 QUEUE 中消费的命令数 */
} bus_servo_axis_state_t;

/* =========================================================================
 * 监督层统计（供 main 验收）
 * ========================================================================= */

/**
 * @brief 监督层统计快照
 */
typedef struct {
    uint32_t command_publishes;       /**< 写入 QUEUE 的命令数 */
    uint32_t state_reads;             /**< 从 LATEST 读取状态次数 */
    uint32_t telem_export_reads;      /**< 导出消费者读取遥测次数 */
    uint32_t telem_monitor_reads;     /**< 监控消费者读取遥测次数 */
    uint32_t fault_events;            /**< 故障事件触发次数 */
} bus_servo_supervisor_metrics_t;

/* =========================================================================
 * 全局对象声明（在 main.c 中定义）
 * ========================================================================= */

extern bus_servo_axis_state_t      g_bus_servo_axis_state;  /**< 速度环运行时状态 */
extern const bm_exec_t             g_bus_servo_axis;        /**< exec 实例句柄 */
extern bus_servo_supervisor_metrics_t g_bus_servo_metrics;  /**< 监督层统计 */

/* bm_bus 句柄（在 main.c 中定义，监督模块通过此访问） */
extern bm_bus_t g_cmd_bus;    /**< QUEUE：指令通道 */
extern bm_bus_t g_state_bus;  /**< LATEST：轴状态 */
extern bm_bus_t g_telem_bus;  /**< SIGNAL：遥测流 */

/* 遥测 SIGNAL 的两个消费者读者句柄 */
extern bm_bus_reader_t g_telem_export_reader;  /**< 遥测导出消费者 */
extern bm_bus_reader_t g_telem_monitor_reader; /**< 遥测监控消费者 */

/* QUEUE 命令通道的控制环消费者 */
extern bm_bus_reader_t g_cmd_reader;   /**< 命令消费者（控制环） */

/* LATEST 状态通道的监督层读者 */
extern bm_bus_reader_t g_state_reader; /**< 状态读者（监督层） */

/* =========================================================================
 * 公共 API（由 main.c 实现，供监督模块调用）
 * ========================================================================= */

/**
 * @brief 向 QUEUE 写入一条指令（监督层调用）
 *
 * 内部执行 acquire_write → 填数据 → commit；QUEUE 满则打日志返回。
 *
 * @param cmd 指令结构体指针，非 NULL
 */
void app_bus_servo_publish_command(const bus_servo_command_t *cmd);

/**
 * @brief 从 LATEST 读取最新轴状态（监督层调用）
 *
 * 内部执行 acquire_read → 拷贝 → release。
 *
 * @param out 输出缓冲，非 NULL
 */
void app_bus_servo_read_state(bus_servo_state_t *out);

/**
 * @brief 排空 SIGNAL 遥测导出消费者（监督层 POLL 回调调用）
 *
 * 循环 acquire_read → release，把本周期内可读遥测帧全部消费。
 * 优雅处理 overflow：检出被绕过时跳到最旧可读槽继续（作为边界演示）。
 *
 * @return 本次实际读到的遥测帧数（正常 + overflow 帧）
 */
uint32_t app_bus_servo_drain_telem_export(void);

/**
 * @brief 排空 SIGNAL 遥测监控消费者（监督层 POLL 回调调用）
 *
 * 与导出消费者独立游标，各自追赶同一 SIGNAL 流。
 *
 * @return 本次实际读到的遥测帧数
 */
uint32_t app_bus_servo_drain_telem_monitor(void);

/**
 * @brief 向 QUEUE 注入故障指令（测试安全停机路径）
 */
void app_bus_servo_inject_fault(void);

#endif /* APP_BUS_SERVO_H */
