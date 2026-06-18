/**
 * @file main.c
 * @brief Buck 双环电源组件示例：电压环 + 电流环 + 一阶 plant
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
#include "app_power.h"
#include "bm_exec.h"
#include "bm_log.h"
#include "bm_module.h"
#include "bm_snapshot.h"
#include "bm_ticker.h"
#include "hybrid_print.h"

#include "hal/bm_hal_timer.h"
#include "hal/bm_hal_uart.h"

#ifdef NATIVE_SIM
#include "bm_hal_timer_native.h"
#endif
#ifdef BM_EXAMPLE_QEMU
#include "qemu_delay.h"
#endif

#include <stdint.h>

#define TAG "power_buck"

#define POWER_VOLTAGE_PERIOD_US   10000u
#define POWER_CURRENT_PERIOD_US   1000u
#define POWER_VOLTAGE_DT_S        0.01f
#define POWER_CURRENT_DT_S        0.001f

#define PLANT_V_TAU_S             0.05f
#define PLANT_I_TAU_S             0.02f
#define PLANT_V_GAIN              1.0f
#define PLANT_I_GAIN              2.0f
#define POWER_PASS_V_MIN          0.5f

BM_SNAPSHOT_DEFINE(power_command, bm_power_ctrl_cmd_t);
BM_SNAPSHOT_DEFINE(power_telemetry, bm_power_ctrl_telemetry_t);

static power_command_snapshot_t g_power_command = BM_SNAPSHOT_INITIALIZER;
static power_telemetry_snapshot_t g_power_telemetry = BM_SNAPSHOT_INITIALIZER;

bm_power_control_axis_t g_power_axis;
power_supervisor_metrics_t g_supervisor_metrics;

static float g_plant_v;
static float g_plant_i;

static const bm_exec_slot_t g_power_slots[] = {
    {
        .kind = BM_EXEC_SLOT_PERIODIC,
        .period_us = POWER_VOLTAGE_PERIOD_US,
        .run = bm_power_control_exec_voltage,
        .name = "voltage_loop"
    },
    {
        .kind = BM_EXEC_SLOT_PERIODIC,
        .period_us = POWER_CURRENT_PERIOD_US,
        .run = bm_power_control_exec_current,
        .name = "current_loop"
    }
};

const bm_exec_t g_power_exec = {
    1u,
    "power_buck",
    &g_power_axis,
    NULL,
    NULL,
    g_power_slots,
    2u,
    NULL,
    0u,
    &bm_power_control_exec_ops
};

static const bm_exec_t *const g_instances[] = { &g_power_exec };

void app_power_publish_command(const bm_power_ctrl_cmd_t *command) {
    if (command != NULL) {
        BM_SNAPSHOT_PUBLISH(g_power_command, command);
    }
}

void app_power_read_telemetry(bm_power_ctrl_telemetry_t *telemetry) {
    if (telemetry != NULL) {
        BM_SNAPSHOT_READ(g_power_telemetry, telemetry);
    }
}

void app_power_inject_fault(void) {
    bm_power_ctrl_cmd_t cmd;

    BM_SNAPSHOT_READ(g_power_command, &cmd);
    cmd.sequence++;
    cmd.status |= BM_POWER_CTRL_CMD_FAULT;
    cmd.v_set_v = 0.0f;
    app_power_publish_command(&cmd);
}

static int read_feedback(void *user, float *v_out_v, float *i_out_a) {
    (void)user;
    *v_out_v = g_plant_v;
    *i_out_a = g_plant_i;
    return 0;
}

static int write_duty(void *user, float duty) {
    float v_target;
    float i_target;

    (void)user;
    v_target = duty * PLANT_V_GAIN;
    i_target = duty * PLANT_I_GAIN;
    g_plant_v += (v_target - g_plant_v) * (POWER_CURRENT_DT_S / PLANT_V_TAU_S);
    g_plant_i += (i_target - g_plant_i) * (POWER_CURRENT_DT_S / PLANT_I_TAU_S);
    return 0;
}

static int read_command_snapshot(void *user, bm_power_ctrl_cmd_t *command) {
    (void)user;
    BM_SNAPSHOT_READ(g_power_command, command);
    return 0;
}

static void publish_telemetry_snapshot(
    void *user,
    const bm_power_ctrl_telemetry_t *telemetry) {
    (void)user;
    BM_SNAPSHOT_PUBLISH(g_power_telemetry, telemetry);
}

static void power_axis_init_defaults(void) {
    bm_power_control_axis_t *axis = &g_power_axis;

    axis->config = (bm_power_control_config_t){
        .pi_voltage = {
            .kp = 2.0f,
            .ki = 10.0f,
            .out_min = -5.0f,
            .out_max = 5.0f,
            .integrator_min = -10.0f,
            .integrator_max = 10.0f
        },
        .pi_current = {
            .kp = 1.0f,
            .ki = 20.0f,
            .out_min = 0.0f,
            .out_max = 1.0f,
            .integrator_min = -2.0f,
            .integrator_max = 2.0f
        },
        .v_ramp = { .rate_per_s = 5.0f },
        .i_limit_a = 5.0f,
        .duty_min = 0.0f,
        .duty_max = 1.0f,
        .voltage_dt_s = POWER_VOLTAGE_DT_S,
        .current_dt_s = POWER_CURRENT_DT_S
    };
    axis->resources.read_feedback = read_feedback;
    axis->resources.write_duty = write_duty;
    axis->resources.read_command = read_command_snapshot;
    axis->resources.publish_telemetry = publish_telemetry_snapshot;
}

#if defined(BM_EXAMPLE_QEMU)
#define POWER_POLL_MS  50u
#else
#define POWER_POLL_MS  100u
#endif

static const bm_ticker_slot_t g_poll_ticker[] = {
    { POWER_POLL_MS, EVENT_POWER_POLL, 1u, "tel_poll" }
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
    bm_power_ctrl_telemetry_t tel;

    BM_LOGI(TAG, "power_buck_loop example start");
    bm_hal_uart_init(NULL);
    power_axis_init_defaults();
    g_plant_v = 0.0f;
    g_plant_i = 0.0f;

    rc = bm_module_boot();
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_POWER_BUCK_LOOP: FAIL boot\n");
        return 1;
    }

    (void)bm_hal_timer_init(1000000u / BM_CONFIG_HRT_TICK_US);

    rc = bm_exec_init_all(g_instances, 1u);
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_POWER_BUCK_LOOP: FAIL init\n");
        return 1;
    }

    rc = bm_exec_start_all(g_instances, 1u);
    if (rc != BM_OK) {
        bm_exec_safe_stop_all(g_instances, 1u);
        hybrid_print("EXAMPLE_POWER_BUCK_LOOP: FAIL start\n");
        return 1;
    }

    rc = bm_ticker_init(g_poll_ticker, 1u);
    if (rc != BM_OK) {
        bm_exec_safe_stop_all(g_instances, 1u);
        hybrid_print("EXAMPLE_POWER_BUCK_LOOP: FAIL ticker\n");
        return 1;
    }

#ifdef BM_EXAMPLE_QEMU
    bm_example_qemu_warmup();
#endif

#ifdef NATIVE_SIM
    run_sim(6000u);
#else
    run_sim(3000u);
#endif

    app_power_read_telemetry(&tel);
    if (g_power_axis.state.voltage_loops < 200u ||
        g_plant_v < POWER_PASS_V_MIN ||
        g_supervisor_metrics.command_publishes == 0u ||
        g_supervisor_metrics.telemetry_reads == 0u) {
        BM_LOGE(TAG, "tracking failed: v=%.3f loops=%u",
                (double)g_plant_v,
                (unsigned)g_power_axis.state.voltage_loops);
        hybrid_print("EXAMPLE_POWER_BUCK_LOOP: FAIL tracking\n");
        bm_exec_safe_stop_all(g_instances, 1u);
        return 1;
    }

    app_power_inject_fault();
#ifdef NATIVE_SIM
    run_sim(200u);
#else
    run_sim(100u);
#endif

    if (g_power_axis.state.fault_latched == 0) {
        hybrid_print("EXAMPLE_POWER_BUCK_LOOP: FAIL fault\n");
        bm_exec_safe_stop_all(g_instances, 1u);
        return 1;
    }

    BM_LOGI(TAG, "ok: v=%.3f duty=%.3f loops=%u",
            (double)g_plant_v, (double)g_power_axis.state.duty,
            (unsigned)g_power_axis.state.voltage_loops);
    hybrid_print_pass("POWER_BUCK_LOOP");

    bm_exec_safe_stop_all(g_instances, 1u);
#ifdef NATIVE_SIM
    return 0;
#else
    while (1) {
    }
#endif
}
