/**
 * @file main.c
 * @brief 闭环伺服示例：ramp → PI → 一阶 plant，验证算法库与框架全链路
 * @author zeh (china_qzh@163.com)
 * @version 1.7
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-17       1.2            zeh            native_sim 仿真周期增至 5500 tick
 * 2026-06-17       1.2            zeh            去除过早 timer init，对齐统一构建 HRT 节拍
 * 2026-06-17       1.3            zeh            native/QEMU 仿真周期对齐速度环验收步数
 * 2026-06-17       1.4            zeh            QEMU bm_hrt_poll、显式 timer init、启动前事件 drain
 * 2026-06-17       1.6            zeh            QEMU 验收阈值与 native 分离（bm_hrt_poll 协作推进）
 * 2026-06-17       1.7            zeh            QEMU 每步 4×spin 推进 TIMER1，周期增至 40000
 */
#include "app_servo.h"
#include "bm_algorithm.h"
#include "bm_exec.h"
#include "bm_hrt.h"
#include "bm_log.h"
#include "bm_module.h"
#include "bm_snapshot.h"
#include "bm_ticker.h"
#include "hybrid_print.h"

#include "bm_hal_adc_sim.h"
#include "bm_hal_pwm_sim.h"
#include "hal/bm_hal_timer.h"
#include "hal/bm_hal_uart.h"

#ifdef NATIVE_SIM
#include "bm_hal_timer_native.h"
#endif
#ifdef BM_EXAMPLE_QEMU
#include "qemu_delay.h"
#endif

#include <stdint.h>

#define TAG "closed_servo"

/** 速度环 Scheduled HRT 周期（us），与 BM_CONFIG_HRT_TICK_US 对齐 */
#define SERVO_LOOP_PERIOD_US    1000u
/** 离散控制周期（s） */
#define SERVO_DT_S              0.001f
/** PWM 满量程计数 */
#define SERVO_PWM_MAX           1000u
/** 速度反馈 ADC 标定：counts / (rad/s) */
#define SERVO_VEL_ADC_SCALE     500.0f

/** 一阶 plant 时间常数（s） */
#define PLANT_TAU_S             0.05f
/** 一阶 plant 稳态增益：velocity = GAIN * duty */
#define PLANT_GAIN              3.0f

/** 跟踪验收：稳态速度下限（rad/s） */
#define SERVO_PASS_VEL_MIN      1.5f

#if defined(BM_EXAMPLE_QEMU)
/** QEMU：TIMER1 靠忙等推进，放宽步数/速度阈值，侧重 ISA 与闭环路径 */
#define SERVO_PASS_LOOP_MIN     100u
#define SERVO_PASS_VEL_MIN_RUN  0.4f
#else
#define SERVO_PASS_LOOP_MIN     500u
#define SERVO_PASS_VEL_MIN_RUN  SERVO_PASS_VEL_MIN
#endif

/** 命令/遥测三缓冲快照盒 */
BM_SNAPSHOT_DEFINE(servo_command, servo_command_t);
BM_SNAPSHOT_DEFINE(servo_telemetry, servo_telemetry_t);

static servo_command_snapshot_t g_servo_command = BM_SNAPSHOT_INITIALIZER;
static servo_telemetry_snapshot_t g_servo_telemetry = BM_SNAPSHOT_INITIALIZER;

servo_axis_state_t g_servo_axis_state;
servo_supervisor_metrics_t g_supervisor_metrics;

/** ramp 配置：限制设定值变化率 */
static bm_algo_ramp_config_t g_ramp_cfg = {
    .rate_per_s = 4.0f
};
static bm_algo_ramp_state_t g_ramp_state;

/** 速度 PI 配置 */
static bm_algo_pi_config_t g_pi_cfg = {
    .kp = 0.8f,
    .ki = 6.0f,
    .out_min = 0.0f,
    .out_max = 1.0f,
    .integrator_min = -2.0f,
    .integrator_max = 2.0f
};
static bm_algo_pi_state_t g_pi_state;

/** native_sim 一阶电机模型内部速度状态 */
static float g_plant_velocity_rad_s;

void app_servo_publish_command(const servo_command_t *command) {
    if (command != NULL) {
        BM_SNAPSHOT_PUBLISH(g_servo_command, command);
    }
}

void app_servo_read_telemetry(servo_telemetry_t *telemetry) {
    if (telemetry != NULL) {
        BM_SNAPSHOT_READ(g_servo_telemetry, telemetry);
    }
}

void app_servo_inject_fault(void) {
    servo_command_t cmd;

    BM_SNAPSHOT_READ(g_servo_command, &cmd);
    cmd.sequence++;
    cmd.status |= SERVO_CMD_STATUS_FAULT;
    cmd.velocity_setpoint_rad_s = 0.0f;
    BM_SNAPSHOT_PUBLISH(g_servo_command, &cmd);
}

/** 速度量纲 → ADC 原始值（仿真注入） */
static uint16_t velocity_to_adc(float velocity_rad_s) {
    float scaled = velocity_rad_s * SERVO_VEL_ADC_SCALE;

    if (scaled < 0.0f) {
        scaled = 0.0f;
    }
    if (scaled > 65535.0f) {
        scaled = 65535.0f;
    }
    return (uint16_t)scaled;
}

/** ADC 原始值 → 速度量纲 */
static float adc_to_velocity(uint16_t raw) {
    return (float)raw / SERVO_VEL_ADC_SCALE;
}

/**
 * @brief 一阶 plant 前向欧拉步进
 *
 * dv/dt = (GAIN * duty - v) / tau；结果写回仿真 ADC rank0。
 */
static void plant_step(float duty) {
    float accel = (PLANT_GAIN * duty - g_plant_velocity_rad_s) / PLANT_TAU_S;

    g_plant_velocity_rad_s += accel * SERVO_DT_S;
    if (g_plant_velocity_rad_s < 0.0f) {
        g_plant_velocity_rad_s = 0.0f;
    }
    bm_hal_adc_sim_set_rank(&BM_HAL_ADC_SIM0, 0u,
                             velocity_to_adc(g_plant_velocity_rad_s));
}

/**
 * @brief 速度环周期回调（Scheduled HRT）
 *
 * 控制链：读命令快照 → ramp → PI → PWM/plant → 发布遥测快照。
 */
static void servo_control_step(const bm_exec_t *instance) {
    servo_axis_state_t *state = (servo_axis_state_t *)instance->state;
    servo_command_t cmd;
    servo_telemetry_t tel;
    uint16_t adc_raw = 0u;
    float velocity_meas;
    float velocity_ref;
    float error;
    float duty_f;
    uint16_t duty_pwm;
    int saturated;

    BM_SNAPSHOT_READ(g_servo_command, &cmd);

    /* 故障命令：安全停机并复位算法状态 */
    if ((cmd.status & SERVO_CMD_STATUS_FAULT) != 0u) {
        state->fault_latched = 1;
        bm_hal_pwm_request_safe_state(&BM_HAL_PWM_SIM0);
        bm_algo_pi_reset(&g_pi_state, 0.0f);
        bm_algo_ramp_reset(&g_ramp_state, g_plant_velocity_rad_s);
        plant_step(0.0f);
        state->fault_count++;

        tel.sequence = state->loop_count;
        tel.status = SERVO_TEL_STATUS_FAULT;
        tel.velocity_meas_rad_s = g_plant_velocity_rad_s;
        tel.velocity_ref_rad_s = 0.0f;
        tel.duty = 0.0f;
        BM_SNAPSHOT_PUBLISH(g_servo_telemetry, &tel);
        state->loop_count++;
        return;
    }

    /* 未使能：零输出 */
    if ((cmd.status & SERVO_CMD_STATUS_ENABLED) == 0u) {
        plant_step(0.0f);
        (void)bm_hal_pwm_set_duty(&BM_HAL_PWM_SIM0, 0u, 0u);
        state->loop_count++;
        return;
    }

    if (bm_hal_adc_read_injected(&BM_HAL_ADC_SIM0, 0u, &adc_raw) != BM_OK) {
        BM_LOGW(TAG, "ADC read failed, request safe state");
        bm_hal_pwm_request_safe_state(&BM_HAL_PWM_SIM0);
        state->fault_latched = 1;
        state->fault_count++;
        return;
    }

    velocity_meas = adc_to_velocity(adc_raw);
    velocity_ref = bm_algo_ramp_step(&g_ramp_state, &g_ramp_cfg,
                                     cmd.velocity_setpoint_rad_s, SERVO_DT_S);
    error = velocity_ref - velocity_meas;
    duty_f = bm_algo_pi_step(&g_pi_state, &g_pi_cfg, error, SERVO_DT_S);
    saturated = (duty_f <= g_pi_cfg.out_min + 1e-6f) ||
                (duty_f >= g_pi_cfg.out_max - 1e-6f);

    plant_step(duty_f);
    duty_pwm = (uint16_t)(duty_f * (float)SERVO_PWM_MAX);
    (void)bm_hal_pwm_set_duty(&BM_HAL_PWM_SIM0, 0u, duty_pwm);

    tel.sequence = state->loop_count;
    tel.status = SERVO_TEL_STATUS_VALID;
    if (saturated) {
        tel.status |= SERVO_TEL_STATUS_SAT;
    }
    tel.velocity_meas_rad_s = velocity_meas;
    tel.velocity_ref_rad_s = velocity_ref;
    tel.duty = duty_f;
    BM_SNAPSHOT_PUBLISH(g_servo_telemetry, &tel);

    state->velocity_meas_rad_s = velocity_meas;
    state->velocity_ref_rad_s = velocity_ref;
    state->duty = duty_f;
    state->loop_count++;
}

static int servo_init(const bm_exec_t *instance) {
    (void)instance;
    bm_algo_ramp_reset(&g_ramp_state, 0.0f);
    bm_algo_pi_reset(&g_pi_state, 0.0f);
    g_plant_velocity_rad_s = 0.0f;
    bm_hal_adc_sim_set_rank(&BM_HAL_ADC_SIM0, 0u, 0u);
    BM_LOGD(TAG, "servo axis init");
    return BM_OK;
}

static int servo_start(const bm_exec_t *instance) {
    (void)instance;
    return bm_hal_pwm_enable_outputs(&BM_HAL_PWM_SIM0, 1);
}

static void servo_safe_stop(const bm_exec_t *instance) {
    (void)instance;
    bm_hal_pwm_request_safe_state(&BM_HAL_PWM_SIM0);
}

static const bm_exec_ops_t g_servo_ops = {
    servo_init, servo_start, servo_safe_stop
};

static const bm_exec_slot_t g_servo_slots[] = {
    {
        .kind = BM_EXEC_SLOT_PERIODIC,
        .period_us = SERVO_LOOP_PERIOD_US,
        .run = servo_control_step,
        .name = "speed_loop"
    }
};

/** 单轴伺服执行实例 */
const bm_exec_t g_servo_axis = {
    .id = 1u,
    .owner_cpu = 0u,
    .name = "servo_axis",
    .state = &g_servo_axis_state,
    .config = NULL,
    .resources = NULL,
    .slots = g_servo_slots,
    .slot_count = 1u,
    .claims = NULL,
    .claim_count = 0u,
    .ops = &g_servo_ops
};

static const bm_exec_t *const g_instances[] = { &g_servo_axis };

#if defined(BM_EXAMPLE_QEMU)
#define SERVO_POLL_MS  10u
#else
#define SERVO_POLL_MS  100u
#endif

/** 监督层遥测轮询 ticker */
static const bm_ticker_slot_t g_poll_ticker[] = {
    { SERVO_POLL_MS, EVENT_SERVO_POLL, 1u, "tel_poll" }
};

/** 推进仿真时钟并调度 ticker / 事件 */
static void run_sim(uint32_t cycles) {
    uint32_t i;

    for (i = 0u; i < cycles; ++i) {
#ifdef NATIVE_SIM
        bm_hal_timer_native_advance_ticks(1u);
#elif defined(BM_EXAMPLE_QEMU)
        {
            uint32_t s;
            for (s = 0u; s < 4u; ++s) {
                bm_example_qemu_spin();
            }
        }
        bm_hrt_poll();
#else
        for (volatile uint32_t d = 0u; d < 20u; ++d) {
        }
#endif
        (void)bm_ticker_poll();
        (void)bm_event_process(8u);
    }
}

int main(void) {
    int rc;

    BM_LOGI(TAG, "closed_loop_servo example start");
    bm_hal_uart_init(NULL);

    rc = bm_module_boot();
    if (rc != BM_OK) {
        BM_LOGE(TAG, "module boot failed, rc=%d", rc);
        hybrid_print("EXAMPLE_CLOSED_LOOP_SERVO: FAIL boot\n");
        return 1;
    }

    (void)bm_hal_timer_init(1000000u / BM_CONFIG_HRT_TICK_US);

    rc = bm_exec_init_all(g_instances, 1u);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "exec init failed, rc=%d", rc);
        hybrid_print("EXAMPLE_CLOSED_LOOP_SERVO: FAIL init\n");
        return 1;
    }

    rc = bm_exec_start_all(g_instances, 1u);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "exec start failed, rc=%d", rc);
        bm_exec_safe_stop_all(g_instances, 1u);
        hybrid_print("EXAMPLE_CLOSED_LOOP_SERVO: FAIL start\n");
        return 1;
    }

    rc = bm_ticker_init(g_poll_ticker, 1u);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "ticker init failed, rc=%d", rc);
        bm_exec_safe_stop_all(g_instances, 1u);
        hybrid_print("EXAMPLE_CLOSED_LOOP_SERVO: FAIL ticker\n");
        return 1;
    }

#ifdef BM_EXAMPLE_QEMU
    bm_example_qemu_warmup();
#endif

    (void)bm_event_process(8u);

#ifdef NATIVE_SIM
    /* 速度环 1 ms / HRT 100 us → 每 10 tick 一步；≥500 步需 ≥5000 tick */
    run_sim(5500u);
#elif defined(BM_EXAMPLE_QEMU)
    run_sim(40000u);
#else
    run_sim(1000u);
#endif

    /* 阶段一：闭环跟踪验收 */
    if (g_servo_axis_state.loop_count < SERVO_PASS_LOOP_MIN ||
        g_servo_axis_state.velocity_meas_rad_s < SERVO_PASS_VEL_MIN_RUN ||
        g_supervisor_metrics.command_publishes == 0u ||
        g_supervisor_metrics.telemetry_reads == 0u) {
        BM_LOGE(TAG,
                "tracking failed: loops=%u vel=%.3f cmd_pub=%u tel_read=%u",
                (unsigned)g_servo_axis_state.loop_count,
                (double)g_servo_axis_state.velocity_meas_rad_s,
                (unsigned)g_supervisor_metrics.command_publishes,
                (unsigned)g_supervisor_metrics.telemetry_reads);
        hybrid_print("EXAMPLE_CLOSED_LOOP_SERVO: FAIL tracking\n");
        bm_exec_safe_stop_all(g_instances, 1u);
        return 1;
    }

    /* 阶段二：故障注入 → 安全停机验收 */
    app_servo_inject_fault();
#ifdef NATIVE_SIM
    run_sim(200u);
#elif defined(BM_EXAMPLE_QEMU)
    run_sim(2000u);
#else
    run_sim(100u);
#endif

    if (g_servo_axis_state.fault_latched == 0 ||
        bm_hal_pwm_sim_outputs_enabled(&BM_HAL_PWM_SIM0)) {
        BM_LOGE(TAG, "fault path failed: latched=%d pwm=%d",
                g_servo_axis_state.fault_latched,
                bm_hal_pwm_sim_outputs_enabled(&BM_HAL_PWM_SIM0));
        hybrid_print("EXAMPLE_CLOSED_LOOP_SERVO: FAIL fault\n");
        bm_exec_safe_stop_all(g_instances, 1u);
        return 1;
    }

    BM_LOGI(TAG,
            "metrics ok: loops=%u vel=%.3f cmd_pub=%u tel_read=%u faults=%u",
            (unsigned)g_servo_axis_state.loop_count,
            (double)g_servo_axis_state.velocity_meas_rad_s,
            (unsigned)g_supervisor_metrics.command_publishes,
            (unsigned)g_supervisor_metrics.telemetry_reads,
            (unsigned)g_servo_axis_state.fault_count);
    hybrid_print_pass("CLOSED_LOOP_SERVO");

    bm_exec_safe_stop_all(g_instances, 1u);
#ifdef NATIVE_SIM
    return 0;
#else
    while (1) {
    }
#endif
}
