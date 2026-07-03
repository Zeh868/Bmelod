/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file balance_schedule.c
 * @brief Balance-car bm_tt_schedule assembly: ISR torque loop + MAINLOOP
 *        telemetry aggregation
 *
 * @details Assembly-file convention sample: zero hardware includes. This
 * file only pulls in `bm_tt_schedule.h`/`bm_bus.h` and assembles buses,
 * step functions, LET task declarations and the schedule table — nothing
 * here depends on HRT/ticker/exec or any hardware driver, so the same
 * pattern applies verbatim on a real MCU target (main.c is the only place
 * that differs between native_sim and a real board: manual tick loop vs.
 * bm_hrt_slot_t + ISR-driven bm_tt_schedule_tick()).
 *
 * Two tasks on one schedule table `ctrl` (minor_us=1000):
 *   - `balance` (ISR domain): every tick reads IMU pitch, computes
 *     `cmd = -kp * pitch` and writes it to the motor bus; falls back to
 *     `cmd = 0` (no torque) when the IMU input is STALE (never published
 *     or expired).
 *   - `telemetry` (MAINLOOP domain): every 10 ticks runs an exponential
 *     moving average over the motor command, demonstrating how to route a
 *     heavier/non-hard-real-time task to the main loop instead of the ISR
 *     time slice.
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-03
 *
 * @par Change Log:
 *
 *    Date         Version        Author          Description
 * 2026-07-03       1.0            zeh            Split out of main.c as the
 *                                                 assembly-file convention
 *                                                 sample
 *
 */
#include "balance_schedule.h"
#include "bm_bus.h"

/** Proportional gain: cmd = -kp * pitch */
#define BALANCE_KP          0.5f
/** Schedule minor tick granularity (us): 1ms; balance runs every tick, telemetry every 10 ticks */
#define CTRL_MINOR_US       1000u
/** Telemetry moving-average window weight (exponential moving average: simple, zero dynamic allocation) */
#define TELEMETRY_EMA_ALPHA 0.2f

/* =========================================================================
 * Buses: imu_bus (input, pitch angle) / motor_bus (output, torque cmd) /
 *        telemetry_bus (output, aggregated telemetry value)
 * ========================================================================= */

BM_BUS_DEFINE(imu_bus, float, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(motor_bus, float, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(telemetry_bus, float, 4u, 1u, BM_BUS_LATEST);

bm_bus_t g_imu_bus;
bm_bus_t g_motor_bus;
bm_bus_t g_telemetry_bus;

static const float k_imu_safe = 0.0f;       /**< IMU STALE fallback: treated as level */
static const float k_motor_safe = 0.0f;     /**< Motor STALE fallback: zero torque, no output */
static const float k_telemetry_safe = 0.0f; /**< Telemetry initial safe value */

/** @brief Persistent state of the telemetry task: exponential moving average accumulator */
typedef struct {
    float ema;
    int   initialized;
} telemetry_state_t;

static telemetry_state_t g_telemetry_state;

/* =========================================================================
 * balance (ISR domain): short proportional-control step, reads IMU, writes
 * motor command
 * ========================================================================= */

static const bm_let_input_t k_balance_inputs[] = {
    { .bus = &g_imu_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(float), .safe_default = &k_imu_safe },
};
static const bm_let_output_t k_balance_outputs[] = {
    { .bus = &g_motor_bus, .elem_size = sizeof(float),
      .safe_default = &k_motor_safe },
};

/** @brief ISR-domain step: cmd = -kp * pitch; falls back to 0 (no torque) when pitch is STALE */
static void balance_step(bm_let_ctx_t *ctx, void *state) {
    int stale;
    uint32_t age_us;
    const float *pitch;
    float *cmd;

    (void)state;
    pitch = (const float *)bm_let_in(ctx, 0u, &stale, &age_us);
    cmd = (float *)bm_let_out(ctx, 0u);

    if (stale) {
        *cmd = 0.0f; /* fail-safe: IMU data expired/never published, no torque output */
    } else {
        *cmd = -BALANCE_KP * (*pitch);
    }
}

BM_LET_DEFINE_ISR(balance, 1u, 0u, 40u, balance_step, NULL,
                   k_balance_inputs, k_balance_outputs);

/* =========================================================================
 * telemetry (MAINLOOP domain): exponential moving average over the motor
 * command every 10 ticks
 * ========================================================================= */

static const bm_let_input_t k_telemetry_inputs[] = {
    { .bus = &g_motor_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(float), .safe_default = &k_motor_safe },
};
static const bm_let_output_t k_telemetry_outputs[] = {
    { .bus = &g_telemetry_bus, .elem_size = sizeof(float),
      .safe_default = &k_telemetry_safe },
};

/** @brief MAINLOOP-domain step: heavier-task sample -- exponential moving average, routed to the main loop instead of the ISR time slice */
static void telemetry_step(bm_let_ctx_t *ctx, void *state) {
    int stale;
    uint32_t age_us;
    const float *cmd;
    float *out;
    telemetry_state_t *st = (telemetry_state_t *)state;

    cmd = (const float *)bm_let_in(ctx, 0u, &stale, &age_us);
    out = (float *)bm_let_out(ctx, 0u);

    if (!st->initialized) {
        st->ema = *cmd;
        st->initialized = 1;
    } else {
        st->ema = st->ema + TELEMETRY_EMA_ALPHA * (*cmd - st->ema);
    }
    *out = st->ema;
}

BM_LET_DEFINE_MAINLOOP(telemetry, 10u, 0u, 200u, telemetry_step,
                        &g_telemetry_state, k_telemetry_inputs,
                        k_telemetry_outputs);

/* =========================================================================
 * Schedule table
 * ========================================================================= */

BM_SCHEDULE_DEFINE(ctrl, CTRL_MINOR_US, &balance, &telemetry);

int balance_schedule_setup(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };

    if (bm_bus_open(&g_imu_bus, &imu_bus_storage, &cfg) != BM_OK ||
        bm_bus_open(&g_motor_bus, &motor_bus_storage, &cfg) != BM_OK ||
        bm_bus_open(&g_telemetry_bus, &telemetry_bus_storage, &cfg) != BM_OK) {
        return 1;
    }
    return 0;
}
