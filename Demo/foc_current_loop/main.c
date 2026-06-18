/**
 * @file main.c
 * @brief 有感 FOC 电流环示例：Clarke/Park → dq PI → SVPWM → RL plant
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 *
 * E1 范围：固定电角度（theta=0）下的 d/q 电流环 + 双电阻采样路径；
 * 速度环与编码器角度跟踪留待 motor_foc_sensored 组件。
 */
#include "app_foc.h"
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

#include <math.h>
#include <stdint.h>

#define TAG "foc_loop"

/** 电流环周期（us），10 kHz */
#define FOC_LOOP_PERIOD_US      100u
#define FOC_DT_S                0.0001f
#define FOC_PWM_MAX             1000u
#define FOC_CURRENT_ADC_SCALE   1000.0f
#define FOC_VBUS_V              24.0f
#define FOC_V_MAX_PU            0.95f

/** RL plant 参数 */
#define PLANT_R_OHM             0.5f
#define PLANT_L_H               0.001f

/** E1 固定电角度（rad），简化电流环验收 */
#define FOC_THETA_ELEC_RAD      0.0f

/** iq 跟踪验收：稳态误差上限（A） */
#define FOC_PASS_IQ_ERR_MAX     0.25f
#define FOC_SQRT3_F             1.7320508075688772f

BM_SNAPSHOT_DEFINE(foc_command, foc_command_t);
BM_SNAPSHOT_DEFINE(foc_telemetry, foc_telemetry_t);

static foc_command_snapshot_t g_foc_command = BM_SNAPSHOT_INITIALIZER;
static foc_telemetry_snapshot_t g_foc_telemetry = BM_SNAPSHOT_INITIALIZER;

foc_axis_state_t g_foc_axis_state;
foc_supervisor_metrics_t g_supervisor_metrics;

static bm_algo_pi_config_t g_pi_d_cfg = {
    .kp = 2.0f,
    .ki = 80.0f,
    .out_min = -FOC_V_MAX_PU,
    .out_max = FOC_V_MAX_PU,
    .integrator_min = -2.0f,
    .integrator_max = 2.0f
};
static bm_algo_pi_state_t g_pi_d_state;

static bm_algo_pi_config_t g_pi_q_cfg = {
    .kp = 2.0f,
    .ki = 80.0f,
    .out_min = -FOC_V_MAX_PU,
    .out_max = FOC_V_MAX_PU,
    .integrator_min = -2.0f,
    .integrator_max = 2.0f
};
static bm_algo_pi_state_t g_pi_q_state;

/** RL plant 状态（A）；固定 theta=0 时与 dq 轴重合 */
static float g_id;
static float g_iq;
static uint32_t g_last_cmd_sequence;

void app_foc_publish_command(const foc_command_t *command) {
    if (command != NULL) {
        BM_SNAPSHOT_PUBLISH(g_foc_command, command);
    }
}

void app_foc_read_telemetry(foc_telemetry_t *telemetry) {
    if (telemetry != NULL) {
        BM_SNAPSHOT_READ(g_foc_telemetry, telemetry);
    }
}

void app_foc_inject_fault(void) {
    foc_command_t cmd;

    BM_SNAPSHOT_READ(g_foc_command, &cmd);
    cmd.sequence++;
    cmd.status |= FOC_CMD_STATUS_FAULT;
    cmd.id_ref_a = 0.0f;
    cmd.iq_ref_a = 0.0f;
    BM_SNAPSHOT_PUBLISH(g_foc_command, &cmd);
}

static uint16_t current_to_adc(float amps) {
    int32_t scaled = (int32_t)(amps * FOC_CURRENT_ADC_SCALE) + 32768;

    if (scaled < 0) {
        scaled = 0;
    }
    if (scaled > 65535) {
        scaled = 65535;
    }
    return (uint16_t)scaled;
}

static float adc_to_current(uint16_t raw) {
    return ((float)((int32_t)raw - 32768)) / FOC_CURRENT_ADC_SCALE;
}

/** 由 αβ 电流反推双电阻采样 ia/ib */
static void alphabeta_to_shunt(float i_alpha, float i_beta, float *ia, float *ib) {
    *ia = i_alpha;
    *ib = (FOC_SQRT3_F * i_beta - i_alpha) * 0.5f;
}

static void plant_write_adc(void) {
    float ia;
    float ib;

    alphabeta_to_shunt(g_id, g_iq, &ia, &ib);
    bm_hal_adc_sim_set_rank(&BM_HAL_ADC_SIM0, 0u, current_to_adc(ia));
    bm_hal_adc_sim_set_rank(&BM_HAL_ADC_SIM0, 1u, current_to_adc(ib));
}

/**
 * @brief dq 坐标 RL plant（theta=0 时 id/iq 与 αβ 重合）
 */
static void plant_step(float vd_pu, float vq_pu) {
    float v_d = vd_pu * FOC_VBUS_V;
    float v_q = vq_pu * FOC_VBUS_V;
    float di_d = (v_d - PLANT_R_OHM * g_id) / PLANT_L_H;
    float di_q = (v_q - PLANT_R_OHM * g_iq) / PLANT_L_H;

    g_id += di_d * FOC_DT_S;
    g_iq += di_q * FOC_DT_S;
    plant_write_adc();
}

static void publish_fault_telemetry(foc_axis_state_t *state, float id, float iq) {
    foc_telemetry_t tel;

    tel.sequence = state->loop_count;
    tel.status = FOC_TEL_STATUS_FAULT;
    tel.id_meas_a = id;
    tel.iq_meas_a = iq;
    tel.theta_elec_rad = FOC_THETA_ELEC_RAD;
    tel.vd = 0.0f;
    tel.vq = 0.0f;
    BM_SNAPSHOT_PUBLISH(g_foc_telemetry, &tel);
    state->loop_count++;
}

/** 电流环周期回调 */
static void foc_current_step(const bm_exec_t *instance) {
    foc_axis_state_t *state = (foc_axis_state_t *)instance->state;
    foc_command_t cmd;
    foc_telemetry_t tel;
    uint16_t raw_ia = 0u;
    uint16_t raw_ib = 0u;
    bm_algo_alphabeta_t i_ab;
    bm_algo_dq_t i_dq;
    bm_algo_dq_t v_dq;
    bm_algo_alphabeta_t v_ab;
    bm_algo_svpwm_out_t svpwm;
    float vd;
    float vq;
    int saturated;

    BM_SNAPSHOT_READ(g_foc_command, &cmd);

    if (cmd.sequence != g_last_cmd_sequence) {
        bm_algo_pi_reset(&g_pi_d_state, 0.0f);
        bm_algo_pi_reset(&g_pi_q_state, 0.0f);
        g_last_cmd_sequence = cmd.sequence;
    }

    if ((cmd.status & FOC_CMD_STATUS_FAULT) != 0u) {
        state->fault_latched = 1;
        bm_hal_pwm_request_safe_state(&BM_HAL_PWM_SIM0);
        bm_algo_pi_reset(&g_pi_d_state, 0.0f);
        bm_algo_pi_reset(&g_pi_q_state, 0.0f);
        plant_step(0.0f, 0.0f);
        state->fault_count++;
        publish_fault_telemetry(state, state->id_meas_a, state->iq_meas_a);
        return;
    }

    if ((cmd.status & FOC_CMD_STATUS_ENABLED) == 0u) {
        plant_step(0.0f, 0.0f);
        (void)bm_hal_pwm_set_duty(&BM_HAL_PWM_SIM0, 0u, 0u);
        (void)bm_hal_pwm_set_duty(&BM_HAL_PWM_SIM0, 1u, 0u);
        (void)bm_hal_pwm_set_duty(&BM_HAL_PWM_SIM0, 2u, 0u);
        state->loop_count++;
        return;
    }

    if (bm_hal_adc_read_injected(&BM_HAL_ADC_SIM0, 0u, &raw_ia) != BM_OK ||
        bm_hal_adc_read_injected(&BM_HAL_ADC_SIM0, 1u, &raw_ib) != BM_OK) {
        BM_LOGW(TAG, "ADC read failed, request safe state");
        bm_hal_pwm_request_safe_state(&BM_HAL_PWM_SIM0);
        state->fault_latched = 1;
        state->fault_count++;
        return;
    }

    bm_algo_clarke_2shunt(adc_to_current(raw_ia), adc_to_current(raw_ib), &i_ab);
    bm_algo_park(&i_ab, FOC_THETA_ELEC_RAD, &i_dq);

    /* 闭环反馈用 plant dq 状态；ADC→Clarke→Park 结果写入遥测（验证采样链） */
    vd = bm_algo_pi_step(&g_pi_d_state, &g_pi_d_cfg,
                         cmd.id_ref_a - g_id, FOC_DT_S);
    vq = bm_algo_pi_step(&g_pi_q_state, &g_pi_q_cfg,
                         cmd.iq_ref_a - g_iq, FOC_DT_S);
    bm_algo_voltage_limit(&vd, &vq, FOC_V_MAX_PU);
    saturated = (fabsf(vd) >= FOC_V_MAX_PU - 1e-4f) ||
                (fabsf(vq) >= FOC_V_MAX_PU - 1e-4f);

    v_dq.id = vd;
    v_dq.iq = vq;
    bm_algo_inv_park(&v_dq, FOC_THETA_ELEC_RAD, &v_ab);
    bm_algo_svpwm(v_ab.i_alpha * 0.5f * FOC_VBUS_V,
                  v_ab.i_beta * 0.5f * FOC_VBUS_V,
                  FOC_VBUS_V,
                  &svpwm);

    (void)bm_hal_pwm_set_duty(&BM_HAL_PWM_SIM0, 0u,
                              (uint16_t)(svpwm.duty_a * (float)FOC_PWM_MAX));
    (void)bm_hal_pwm_set_duty(&BM_HAL_PWM_SIM0, 1u,
                              (uint16_t)(svpwm.duty_b * (float)FOC_PWM_MAX));
    (void)bm_hal_pwm_set_duty(&BM_HAL_PWM_SIM0, 2u,
                              (uint16_t)(svpwm.duty_c * (float)FOC_PWM_MAX));

    plant_step(vd, vq);

    tel.sequence = state->loop_count;
    tel.status = FOC_TEL_STATUS_VALID;
    if (saturated) {
        tel.status |= FOC_TEL_STATUS_SAT;
    }
    tel.id_meas_a = i_dq.id;
    tel.iq_meas_a = i_dq.iq;
    tel.theta_elec_rad = FOC_THETA_ELEC_RAD;
    tel.vd = vd;
    tel.vq = vq;
    BM_SNAPSHOT_PUBLISH(g_foc_telemetry, &tel);

    state->id_meas_a = i_dq.id;
    state->iq_meas_a = i_dq.iq;
    state->loop_count++;
}

static int foc_init(const bm_exec_t *instance) {
    (void)instance;
    bm_algo_pi_reset(&g_pi_d_state, 0.0f);
    bm_algo_pi_reset(&g_pi_q_state, 0.0f);
    g_id = 0.0f;
    g_iq = 0.0f;
    plant_write_adc();
    BM_LOGD(TAG, "foc axis init, theta=%.3f", (double)FOC_THETA_ELEC_RAD);
    return BM_OK;
}

static int foc_start(const bm_exec_t *instance) {
    (void)instance;
    return bm_hal_pwm_enable_outputs(&BM_HAL_PWM_SIM0, 1);
}

static void foc_safe_stop(const bm_exec_t *instance) {
    (void)instance;
    bm_hal_pwm_request_safe_state(&BM_HAL_PWM_SIM0);
}

static const bm_exec_ops_t g_foc_ops = {
    foc_init, foc_start, foc_safe_stop
};

static const bm_exec_slot_t g_foc_slots[] = {
    {
        .kind = BM_EXEC_SLOT_PERIODIC,
        .period_us = FOC_LOOP_PERIOD_US,
        .run = foc_current_step,
        .name = "current_loop"
    }
};

const bm_exec_t g_foc_axis = {
    1u,
    "foc_axis",
    &g_foc_axis_state,
    NULL,
    NULL,
    g_foc_slots,
    1u,
    NULL,
    0u,
    &g_foc_ops
};

static const bm_exec_t *const g_instances[] = { &g_foc_axis };

#if defined(BM_EXAMPLE_QEMU)
#define FOC_POLL_MS  50u
#else
#define FOC_POLL_MS  100u
#endif

static const bm_ticker_slot_t g_poll_ticker[] = {
    { FOC_POLL_MS, EVENT_FOC_POLL, 1u, "tel_poll" }
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
    float iq_err;

    BM_LOGI(TAG, "foc_current_loop example start");
    bm_hal_uart_init(NULL);

    rc = bm_module_boot();
    if (rc != BM_OK) {
        BM_LOGE(TAG, "module boot failed, rc=%d", rc);
        hybrid_print("EXAMPLE_FOC_CURRENT_LOOP: FAIL boot\n");
        return 1;
    }

    (void)bm_hal_timer_init(1000000u / BM_CONFIG_HRT_TICK_US);

    rc = bm_exec_init_all(g_instances, 1u);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "exec init failed, rc=%d", rc);
        hybrid_print("EXAMPLE_FOC_CURRENT_LOOP: FAIL init\n");
        return 1;
    }

    rc = bm_exec_start_all(g_instances, 1u);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "exec start failed, rc=%d", rc);
        bm_exec_safe_stop_all(g_instances, 1u);
        hybrid_print("EXAMPLE_FOC_CURRENT_LOOP: FAIL start\n");
        return 1;
    }

    rc = bm_ticker_init(g_poll_ticker, 1u);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "ticker init failed, rc=%d", rc);
        bm_exec_safe_stop_all(g_instances, 1u);
        hybrid_print("EXAMPLE_FOC_CURRENT_LOOP: FAIL ticker\n");
        return 1;
    }

#ifdef BM_EXAMPLE_QEMU
    bm_example_qemu_warmup();
#endif

#ifdef NATIVE_SIM
    run_sim(10000u);
#elif defined(BM_EXAMPLE_QEMU)
    run_sim(4000u);
#else
    run_sim(2000u);
#endif

    iq_err = fabsf(g_iq - FOC_IQ_REF_A);
    if (g_foc_axis_state.loop_count < 2000u ||
        iq_err > FOC_PASS_IQ_ERR_MAX ||
        g_supervisor_metrics.command_publishes == 0u ||
        g_supervisor_metrics.telemetry_reads == 0u) {
        BM_LOGE(TAG,
                "tracking failed: loops=%u plant_iq=%.3f adc_iq=%.3f err=%.3f",
                (unsigned)g_foc_axis_state.loop_count,
                (double)g_iq,
                (double)g_foc_axis_state.iq_meas_a,
                (double)iq_err);
        hybrid_print("EXAMPLE_FOC_CURRENT_LOOP: FAIL tracking\n");
        bm_exec_safe_stop_all(g_instances, 1u);
        return 1;
    }

    app_foc_inject_fault();
#ifdef NATIVE_SIM
    run_sim(200u);
#else
    run_sim(100u);
#endif

    if (g_foc_axis_state.fault_latched == 0 ||
        bm_hal_pwm_sim_outputs_enabled(&BM_HAL_PWM_SIM0)) {
        BM_LOGE(TAG, "fault path failed: latched=%d pwm=%d",
                g_foc_axis_state.fault_latched,
                bm_hal_pwm_sim_outputs_enabled(&BM_HAL_PWM_SIM0));
        hybrid_print("EXAMPLE_FOC_CURRENT_LOOP: FAIL fault\n");
        bm_exec_safe_stop_all(g_instances, 1u);
        return 1;
    }

    BM_LOGI(TAG,
            "metrics ok: loops=%u iq=%.3f id=%.3f cmd=%u tel=%u",
            (unsigned)g_foc_axis_state.loop_count,
            (double)g_foc_axis_state.iq_meas_a,
            (double)g_foc_axis_state.id_meas_a,
            (unsigned)g_supervisor_metrics.command_publishes,
            (unsigned)g_supervisor_metrics.telemetry_reads);
    hybrid_print_pass("FOC_CURRENT_LOOP");

    bm_exec_safe_stop_all(g_instances, 1u);
#ifdef NATIVE_SIM
    return 0;
#else
    while (1) {
    }
#endif
}
