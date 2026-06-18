/**
 * @file app_servo.h
 * @brief closed_loop_servo 示例：命令/遥测快照与监督层共享接口
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 *
 */
#ifndef APP_SERVO_H
#define APP_SERVO_H

#include "bm_exec.h"

#include <stdint.h>

/** 监督层事件：使能伺服并发布默认设定值 */
#define EVENT_SERVO_ENABLE   1u
/** 监督层事件：更新速度设定值（载荷可为 float） */
#define EVENT_SERVO_SETPOINT 2u
/** 监督层事件：请求故障停机 */
#define EVENT_SERVO_FAULT    3u
/** 监督层事件：轮询 HRT 遥测快照 */
#define EVENT_SERVO_POLL     4u

/** 默认速度设定值（rad/s），与 native_sim plant 参数匹配 */
#define SERVO_SETPOINT_RAD_S  2.0f

/** 命令快照：轴已使能 */
#define SERVO_CMD_STATUS_ENABLED  (1u << 0u)
/** 命令快照：故障锁存 */
#define SERVO_CMD_STATUS_FAULT    (1u << 1u)

/** 遥测快照：本周期测量有效 */
#define SERVO_TEL_STATUS_VALID    (1u << 0u)
/** 遥测快照：PI 输出饱和 */
#define SERVO_TEL_STATUS_SAT      (1u << 1u)
/** 遥测快照：故障态 */
#define SERVO_TEL_STATUS_FAULT    (1u << 2u)

/** SRT → HRT 命令快照（监督层写、速度环读） */
typedef struct {
    uint32_t sequence;               /**< 递增序号，用于检测更新 */
    uint32_t status;                 /**< SERVO_CMD_STATUS_* */
    float    velocity_setpoint_rad_s;/**< 速度设定（rad/s） */
} servo_command_t;

/** HRT → SRT 遥测快照（速度环写、监督层读） */
typedef struct {
    uint32_t sequence;
    uint32_t status;                 /**< SERVO_TEL_STATUS_* */
    float    velocity_meas_rad_s;    /**< ADC 反馈速度 */
    float    velocity_ref_rad_s;     /**< ramp 输出参考 */
    float    duty;                   /**< PI 输出占空比 [0,1] */
} servo_telemetry_t;

/** bm_exec 速度环运行时状态 */
typedef struct {
    uint32_t loop_count;
    float    velocity_meas_rad_s;
    float    velocity_ref_rad_s;
    float    duty;
    uint32_t fault_count;
    int      fault_latched;
} servo_axis_state_t;

/** 监督层统计（供 main 验收） */
typedef struct {
    uint32_t command_publishes;
    uint32_t telemetry_reads;
    uint32_t fault_events;
} servo_supervisor_metrics_t;

extern servo_axis_state_t g_servo_axis_state;
extern const bm_exec_t g_servo_axis;
extern servo_supervisor_metrics_t g_supervisor_metrics;

/** 发布命令快照（SRT 侧） */
void app_servo_publish_command(const servo_command_t *command);
/** 读取遥测快照（SRT 侧） */
void app_servo_read_telemetry(servo_telemetry_t *telemetry);
/** 注入故障命令（测试安全停机路径） */
void app_servo_inject_fault(void);

#endif /* APP_SERVO_H */
