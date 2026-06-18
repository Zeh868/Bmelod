/**
 * @file main.c
 * @brief BMS SOC 估算组件示例：库仑 + 静置 OCV 融合 + 温度补偿
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
#include "app_bms_est.h"
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

#define TAG "bms_est"

#define BMS_EST_PERIOD_US         100000u
#define BMS_EST_DT_S              0.1f
#define BMS_PASS_SOC_MIN          0.52f

static const float s_soc_lut[] = { 0.0f, 0.5f, 1.0f };
static const float s_ocv_lut[] = { 3.0f, 3.7f, 4.2f };
static const bm_algo_ocv_table_t s_ocv_table = {
    s_soc_lut, s_ocv_lut, 3u
};

BM_SNAPSHOT_DEFINE(bms_telemetry, bm_bms_est_telemetry_t);

static bms_telemetry_snapshot_t g_bms_telemetry = BM_SNAPSHOT_INITIALIZER;

bm_bms_estimation_axis_t g_bms_axis;
bms_est_supervisor_metrics_t g_supervisor_metrics;

static float g_sim_soc = 0.5f;
static float g_sim_current_a;
static float g_sim_voltage_v = 3.7f;
static float g_sim_temp_c = 25.0f;
static uint32_t g_sim_step;

static const bm_exec_slot_t g_bms_slots[] = {
    {
        .kind = BM_EXEC_SLOT_PERIODIC,
        .period_us = BMS_EST_PERIOD_US,
        .run = bm_bms_estimation_exec_step,
        .name = "soc_est"
    }
};

const bm_exec_t g_bms_exec = {
    1u,
    "bms_est",
    &g_bms_axis,
    NULL,
    NULL,
    g_bms_slots,
    1u,
    NULL,
    0u,
    &bm_bms_estimation_exec_ops
};

static const bm_exec_t *const g_instances[] = { &g_bms_exec };

void app_bms_est_read_telemetry(bm_bms_est_telemetry_t *telemetry) {
    if (telemetry != NULL) {
        BM_SNAPSHOT_READ(g_bms_telemetry, telemetry);
    }
}

static int read_sample(void *user,
                       float *pack_current_a,
                       float *pack_voltage_v,
                       float *temp_c) {
    (void)user;
    *pack_current_a = g_sim_current_a;
    *pack_voltage_v = g_sim_voltage_v;
    *temp_c = g_sim_temp_c;
    return 0;
}

static void publish_telemetry_snapshot(
    void *user,
    const bm_bms_est_telemetry_t *telemetry) {
    (void)user;
    BM_SNAPSHOT_PUBLISH(g_bms_telemetry, telemetry);
}

static void sim_plant_step(void) {
    g_sim_step++;
    if ((g_sim_step % 100u) != 0u) {
        return;
    }

    /* 前 150 个估算周期：20A 充电 */
    if (g_sim_step <= 15000u) {
        g_sim_current_a = 20.0f;
        g_sim_soc += (20.0f * BMS_EST_DT_S) / 3600.0f;
        if (g_sim_soc > 0.65f) {
            g_sim_soc = 0.65f;
        }
        g_sim_voltage_v = 3.0f + g_sim_soc * 1.2f;
        return;
    }

    /* 之后静置：电流近零，电压按真实 SOC 缓变 */
    g_sim_current_a = 0.0f;
    g_sim_voltage_v = 3.0f + g_sim_soc * 1.2f;
}

static void bms_axis_init_defaults(void) {
    bm_bms_estimation_axis_t *axis = &g_bms_axis;

    axis->config = (bm_bms_estimation_config_t){
        .coulomb = {
            .nominal_capacity_ah = 1.0f,
            .coulomb_efficiency = 1.0f,
            .soc_min = 0.0f,
            .soc_max = 1.0f
        },
        .temp = {
            .ref_temp_c = 25.0f,
            .capacity_coeff_per_c = 0.005f,
            .ocv_shift_v_per_c = -0.002f
        },
        .fusion = { .ocv_weight = 0.3f },
        .ocv_table = &s_ocv_table,
        .resting_current_a = 0.05f,
        .resting_time_s = 1.0f,
        .dt_s = BMS_EST_DT_S,
        .soc_init = 0.5f
    };
    axis->resources.read_sample = read_sample;
    axis->resources.publish_telemetry = publish_telemetry_snapshot;
}

#if defined(BM_EXAMPLE_QEMU)
#define BMS_POLL_MS  200u
#else
#define BMS_POLL_MS  500u
#endif

static const bm_ticker_slot_t g_poll_ticker[] = {
    { BMS_POLL_MS, EVENT_BMS_EST_POLL, 1u, "tel_poll" }
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
        sim_plant_step();
        (void)bm_ticker_poll();
        (void)bm_event_process(8u);
    }
}

int main(void) {
    int rc;

    BM_LOGI(TAG, "bms_estimation_loop example start");
    bm_hal_uart_init(NULL);
    bms_axis_init_defaults();
    g_sim_step = 0u;

    rc = bm_module_boot();
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_BMS_ESTIMATION_LOOP: FAIL boot\n");
        return 1;
    }

    (void)bm_hal_timer_init(1000000u / BM_CONFIG_HRT_TICK_US);

    rc = bm_exec_init_all(g_instances, 1u);
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_BMS_ESTIMATION_LOOP: FAIL init\n");
        return 1;
    }

    rc = bm_exec_start_all(g_instances, 1u);
    if (rc != BM_OK) {
        bm_exec_safe_stop_all(g_instances, 1u);
        hybrid_print("EXAMPLE_BMS_ESTIMATION_LOOP: FAIL start\n");
        return 1;
    }

    rc = bm_ticker_init(g_poll_ticker, 1u);
    if (rc != BM_OK) {
        bm_exec_safe_stop_all(g_instances, 1u);
        hybrid_print("EXAMPLE_BMS_ESTIMATION_LOOP: FAIL ticker\n");
        return 1;
    }

#ifdef BM_EXAMPLE_QEMU
    bm_example_qemu_warmup();
#endif

#ifdef NATIVE_SIM
    run_sim(8000u);
#else
    run_sim(4000u);
#endif

    if (g_bms_axis.state.step_count < 50u ||
        g_bms_axis.state.soc_fused < BMS_PASS_SOC_MIN ||
        g_supervisor_metrics.telemetry_reads == 0u) {
        BM_LOGE(TAG, "estimation failed: soc=%.3f steps=%u",
                (double)g_bms_axis.state.soc_fused,
                (unsigned)g_bms_axis.state.step_count);
        hybrid_print("EXAMPLE_BMS_ESTIMATION_LOOP: FAIL tracking\n");
        bm_exec_safe_stop_all(g_instances, 1u);
        return 1;
    }

    BM_LOGI(TAG, "ok: soc=%.3f steps=%u tel=%u",
            (double)g_bms_axis.state.soc_fused,
            (unsigned)g_bms_axis.state.step_count,
            (unsigned)g_supervisor_metrics.telemetry_reads);
    hybrid_print_pass("BMS_ESTIMATION_LOOP");

    bm_exec_safe_stop_all(g_instances, 1u);
#ifdef NATIVE_SIM
    return 0;
#else
    while (1) {
    }
#endif
}
