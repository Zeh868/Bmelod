/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file balance_schedule.h
 * @brief Public interface of the balance-car bm_tt_schedule assembly (ISR
 *        torque loop + MAINLOOP telemetry aggregation)
 *
 * @details Declares the `ctrl` schedule table assembled in
 * balance_schedule.c (task "balance", ISR domain; task "telemetry",
 * MAINLOOP domain) and the setup hook that opens every bus the table's
 * tasks bind to. main.c drives the demo via this header plus
 * `bm_tt_schedule_init()` / `bm_tt_schedule_tick()` /
 * `bm_tt_schedule_run_pending()`; the three bus handles are also exported
 * here because this native_sim demo has no real IMU/motor hardware — the
 * tick loop in main.c injects synthetic IMU samples and reads back
 * cmd/telemetry through them directly to print the data flow. A real
 * deployment would not need this: sensors/actuators publish/subscribe to
 * these buses from their own driver code instead of from main().
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
#ifndef BALANCE_SCHEDULE_H
#define BALANCE_SCHEDULE_H

#include "bm_tt_schedule.h"
#include "bm_bus.h"

/** @brief The demo's schedule table: minor=1000us, "balance" (ISR) + "telemetry" (MAINLOOP) */
extern bm_tt_schedule_t ctrl;

/** @brief IMU input bus (pitch angle, rad); main.c injects synthetic samples on it */
extern bm_bus_t g_imu_bus;
/** @brief Motor output bus (torque cmd); main.c reads it back for printing */
extern bm_bus_t g_motor_bus;
/** @brief Telemetry output bus (moving-average value); main.c reads it back for printing */
extern bm_bus_t g_telemetry_bus;

/**
 * @brief Open every bus used by the `ctrl` schedule table
 *
 * @return 0 on success; non-zero if any bm_bus_open() call fails
 */
int balance_schedule_setup(void);

#endif /* BALANCE_SCHEDULE_H */
