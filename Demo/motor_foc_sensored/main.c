/**
 * @file main.c
 * @brief 有感 FOC 组件示例：电流环 + 编码器速度环 + RL/机械 plant
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
#include "app_motor.h"
#include "bm_exec.h"
#include "bm_log.h"
#include "bm_module.h"
#include "bm_snapshot.h"
#include "bm_ticker.h"
#include "hybrid_print.h"

#include "bm_hal_adc_sim.h"
#include "bm_hal_encoder_sim.h"
#include "bm_hal_pwm_sim.h"
#include "hal/bm_hal_timer.h"
#include "hal/bm_hal_uart.h"

#ifdef NATIVE_SIM
#include "bm_hal_timer_native.h"
#endif
#ifdef BM_EXAMPLE_QEMU
#include "qemu_delay.h"
#endif

#include <math.h>
#include <stdint.h>

#define TAG "motor_foc"

#define MOTOR_CURRENT_PERIOD_US   100u
#define MOTOR_SPEED_PERIOD_US     1000u
#define MOTOR_CURRENT_DT_S        0.0001f
#define MOTOR_SPEED_DT_S          0.001f

#define PLANT_POLE_PAIRS          4.0f
#define PLANT_ENCODER_CPR         4096u
#define PLANT_R_OHM               0.5f
#define PLANT_L_H                 0.001f
#define PLANT_VBUS_V              24.0f
#define PLANT_J_KGM2              0.0005f
#define PLANT_B_NMS                 0.0002f
#define PLANT_KT_NM_A             0.02f
#define PLANT_CURRENT_ADC_SCALE   1000.0f
#define PLANT_PWM_MAX             1000u
#define PLANT_SQRT3_F             1.7320508075688772f

BM_SNAPSHOT_DEFINE(motor_command, bm_motor_foc_cmd_t);
BM_SNAPSHOT_DEFINE(motor_telemetry, bm_motor_foc_telemetry_t);

static motor_command_snapshot_t g_motor_command = BM_SNAPSHOT_INITIALIZER;
static motor_telemetry_snapshot_t g_motor_telemetry = BM_SNAPSHOT_INITIALIZER;

bm_motor_foc_sensored_axis_t g_motor_axis;
motor_supervisor_metrics_t g_supervisor_metrics;

static float g_plant_id;
static float g_plant_iq;
static float g_plant_theta_mech;
static float g_plant_speed_mech;
static float g_plant_theta_elec;

static const bm_exec_slot_t g_motor_slots[] = {
    {
        .kind = BM_EXEC_SLOT_PERIODIC,
        .period_us = MOTOR_CURRENT_PERIOD_US,
        .run = bm_motor_foc_sensored_exec_current,
        .name = "current_loop"
    },
    {
        .kind = BM_EXEC_SLOT_PERIODIC,
        .period_us = MOTOR_SPEED_PERIOD_US,
        .run = bm_motor_foc_sensored_exec_speed,
        .name = "speed_loop"
    }
};

const bm_exec_t g_motor_exec = {
    1u,
    "motor_foc",
    &g_motor_axis,
    NULL,
    NULL,
    g_motor_slots,
    2u,
    NULL,
    0u,
    &bm_motor_foc_sensored_exec_ops
};

static const bm_exec_t *const g_instances[] = { &g_motor_exec };

void app_motor_publish_command(const bm_motor_foc_cmd_t *command) {
    if (command != NULL) {
        BM_SNAPSHOT_PUBLISH(g_motor_command, command);
    }
}

void app_motor_read_telemetry(bm_motor_foc_telemetry_t *telemetry) {
    if (telemetry != NULL) {
        BM_SNAPSHOT_READ(g_motor_telemetry, telemetry);
    }
}

void app_motor_inject_fault(void) {
    bm_motor_foc_cmd_t cmd;

    BM_SNAPSHOT_READ(g_motor_command, &cmd);
    cmd.sequence++;
    cmd.status |= BM_MOTOR_FOC_CMD_FAULT;
    cmd.speed_setpoint_rad_s = 0.0f;
    app_motor_publish_command(&cmd);
}

static uint16_t current_to_adc(float amps) {
    int32_t scaled = (int32_t)(amps * PLANT_CURRENT_ADC_SCALE) + 32768;

    if (scaled < 0) {
        scaled = 0;
    }
    if (scaled > 65535) {
        scaled = 65535;
    }
    return (uint16_t)scaled;
}

static void plant_write_adc(void) {
    float ia;
    float ib;

    ia = g_plant_id;
    ib = (PLANT_SQRT3_F * g_plant_iq - ia) * 0.5f;
    bm_hal_adc_sim_set_rank(&BM_HAL_ADC_SIM0, 0u, current_to_adc(ia));
    bm_hal_adc_sim_set_rank(&BM_HAL_ADC_SIM0, 1u, current_to_adc(ib));
}

static void plant_on_voltage(void *user, float vd_pu, float vq_pu,
                             float theta_elec) {
    float v_d;
    float v_q;
    float torque;
    float di_d;
    float di_q;

    (void)user;
    (void)theta_elec;

    v_d = vd_pu * PLANT_VBUS_V;
    v_q = vq_pu * PLANT_VBUS_V;
    di_d = (v_d - PLANT_R_OHM * g_plant_id) / PLANT_L_H;
    di_q = (v_q - PLANT_R_OHM * g_plant_iq) / PLANT_L_H;
    g_plant_id += di_d * MOTOR_CURRENT_DT_S;
    g_plant_iq += di_q * MOTOR_CURRENT_DT_S;

    torque = PLANT_KT_NM_A * g_plant_iq;
    g_plant_speed_mech += (torque - PLANT_B_NMS * g_plant_speed_mech) /
                          PLANT_J_KGM2 * MOTOR_CURRENT_DT_S;
    g_plant_theta_mech += g_plant_speed_mech * MOTOR_CURRENT_DT_S;
    g_plant_theta_elec = PLANT_POLE_PAIRS * g_plant_theta_mech;

    plant_write_adc();
    bm_hal_encoder_sim_set_count(
        &BM_HAL_ENC_SIM0,
        (int32_t)(fmodf(g_plant_theta_mech, 6.283185307f) *
                  (float)PLANT_ENCODER_CPR / 6.283185307f));
}

static int read_command_snapshot(void *user, bm_motor_foc_cmd_t *command) {
    (void)user;
    BM_SNAPSHOT_READ(g_motor_command, command);
    return 0;
}

static void publish_telemetry_snapshot(
    void *user,
    const bm_motor_foc_telemetry_t *telemetry) {
    (void)user;
    BM_SNAPSHOT_PUBLISH(g_motor_telemetry, telemetry);
}

static void motor_axis_init_defaults(void) {
    bm_motor_foc_sensored_axis_t *axis = &g_motor_axis;

    axis->config = (bm_motor_foc_sensored_config_t){
        .pole_pairs = PLANT_POLE_PAIRS,
        .encoder_direction = 1.0f,
        .electrical_offset_rad = 0.0f,
        .vbus_v = PLANT_VBUS_V,
        .phase_r_ohm = PLANT_R_OHM,
        .v_max_pu = 0.95f,
        .current_dt_s = MOTOR_CURRENT_DT_S,
        .speed_dt_s = MOTOR_SPEED_DT_S,
        .iq_max_a = 2.0f,
        .pi_d = {
            .kp = 2.0f, .ki = 80.0f,
            .out_min = -0.95f, .out_max = 0.95f,
            .integrator_min = -2.0f, .integrator_max = 2.0f
        },
        .pi_q = {
            .kp = 2.0f, .ki = 80.0f,
            .out_min = -0.95f, .out_max = 0.95f,
            .integrator_min = -2.0f, .integrator_max = 2.0f
        },
        .pi_speed = {
            .kp = 0.15f, .ki = 3.0f,
            .out_min = -2.0f, .out_max = 2.0f,
            .integrator_min = -5.0f, .integrator_max = 5.0f
        },
        .speed_ramp = { .rate_per_s = 10.0f },
        .encoder = { .counts_per_rev = PLANT_ENCODER_CPR }
    };

    axis->resources = (bm_motor_foc_sensored_resources_t){
        .adc = &BM_HAL_ADC_SIM0,
        .pwm = &BM_HAL_PWM_SIM0,
        .encoder = &BM_HAL_ENC_SIM0,
        .adc_rank_ia = 0u,
        .adc_rank_ib = 1u,
        .pwm_max = PLANT_PWM_MAX,
        .current_adc_scale = PLANT_CURRENT_ADC_SCALE,
        .sim_fb = {
            .id_a = &g_plant_id,
            .iq_a = &g_plant_iq,
            .theta_elec_rad = &g_plant_theta_elec
        },
        .on_voltage = plant_on_voltage,
        .on_voltage_user = NULL,
        .read_command = read_command_snapshot,
        .read_command_user = NULL,
        .publish_telemetry = publish_telemetry_snapshot,
        .publish_telemetry_user = NULL
    };
}

#if defined(BM_EXAMPLE_QEMU)
#define MOTOR_POLL_MS  50u
#else
#define MOTOR_POLL_MS  100u
#endif

static const bm_ticker_slot_t g_poll_ticker[] = {
    { MOTOR_POLL_MS, EVENT_MOTOR_POLL, 1u, "tel_poll" }
};

static void run_sim(uint32_t cycles) {
    uint32_t i;

    for (i = 0u; i < cycles; ++i) {
#ifdef NATIVE_SIM
        bm_hal_timer_native_advance_ticks(1u);
#elif defined(BM_EXAMPLE_QEMU)
        bm_example_qemu_spin();
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
    float speed_meas;

    BM_LOGI(TAG, "motor_foc_sensored example start");
    bm_hal_uart_init(NULL);
    motor_axis_init_defaults();
    plant_write_adc();
    bm_hal_encoder_sim_set_count(&BM_HAL_ENC_SIM0, 0);

    rc = bm_module_boot();
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_MOTOR_FOC_SENSORED: FAIL boot\n");
        return 1;
    }

    (void)bm_hal_timer_init(1000000u / BM_CONFIG_HRT_TICK_US);

    rc = bm_exec_init_all(g_instances, 1u);
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_MOTOR_FOC_SENSORED: FAIL init\n");
        return 1;
    }

    rc = bm_exec_start_all(g_instances, 1u);
    if (rc != BM_OK) {
        bm_exec_safe_stop_all(g_instances, 1u);
        hybrid_print("EXAMPLE_MOTOR_FOC_SENSORED: FAIL start\n");
        return 1;
    }

    rc = bm_ticker_init(g_poll_ticker, 1u);
    if (rc != BM_OK) {
        bm_exec_safe_stop_all(g_instances, 1u);
        hybrid_print("EXAMPLE_MOTOR_FOC_SENSORED: FAIL ticker\n");
        return 1;
    }

#ifdef BM_EXAMPLE_QEMU
    bm_example_qemu_warmup();
#endif

#ifdef NATIVE_SIM
    run_sim(20000u);
#elif defined(BM_EXAMPLE_QEMU)
    run_sim(10000u);
#else
    run_sim(5000u);
#endif

    speed_meas = g_motor_axis.state.speed.encoder.velocity_rad_s;
    if (g_motor_axis.state.current.loop_count < 5000u ||
        speed_meas < MOTOR_PASS_SPEED_MIN ||
        g_supervisor_metrics.command_publishes == 0u ||
        g_supervisor_metrics.telemetry_reads == 0u) {
        BM_LOGE(TAG, "tracking failed: loops=%u speed=%.3f",
                (unsigned)g_motor_axis.state.current.loop_count,
                (double)speed_meas);
        hybrid_print("EXAMPLE_MOTOR_FOC_SENSORED: FAIL tracking\n");
        bm_exec_safe_stop_all(g_instances, 1u);
        return 1;
    }

    app_motor_inject_fault();
#ifdef NATIVE_SIM
    run_sim(300u);
#else
    run_sim(150u);
#endif

    if (g_motor_axis.state.fault_latched == 0 ||
        bm_hal_pwm_sim_outputs_enabled(&BM_HAL_PWM_SIM0)) {
        hybrid_print("EXAMPLE_MOTOR_FOC_SENSORED: FAIL fault\n");
        bm_exec_safe_stop_all(g_instances, 1u);
        return 1;
    }

    BM_LOGI(TAG, "metrics ok: loops=%u speed=%.3f iq=%.3f",
            (unsigned)g_motor_axis.state.current.loop_count,
            (double)speed_meas,
            (double)g_motor_axis.state.telemetry.iq_meas_a);
    hybrid_print_pass("MOTOR_FOC_SENSORED");

    bm_exec_safe_stop_all(g_instances, 1u);
#ifdef NATIVE_SIM
    return 0;
#else
    while (1) {
    }
#endif
}
