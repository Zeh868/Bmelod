/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file balance_schedule.c
 * @brief 平衡车 bm_tt_schedule 装配：ISR 力矩环 + MAINLOOP 遥测聚合
 *
 * @details 装配文件惯例样例：零硬件 include。本文件只引入
 * `bm_tt_schedule.h`/`bm_bus.h`，装配总线、step 函数、LET 任务声明与
 * 调度表——这里的一切都不依赖 HRT/ticker/exec 或任何硬件驱动，因此同一套
 * 模式可原样搬到真实 MCU 目标上（main.c 是 native_sim 与真实板子之间唯一
 * 不同之处：手工 tick 循环 vs. bm_hrt_slot_t + ISR 驱动的
 * bm_tt_schedule_tick()）。
 *
 * 一张调度表 `ctrl`（minor_us=1000）上的两个任务：
 *   - `balance`（ISR 域）：每拍读取 IMU 俯仰角，算出
 *     `cmd = -kp * pitch` 并写入电机总线；当 IMU 输入 STALE（从未发布或
 *     已过期）时兜底为 `cmd = 0`（不出力）。
 *   - `telemetry`（MAINLOOP 域）：每 10 拍对电机指令做一次指数滑动平均，
 *     演示如何把较重/非硬实时的任务路由到主循环而非 ISR 时间片。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-03
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-03       1.0            zeh            从 main.c 拆出，作为装配
 *                                                 文件惯例样例
 * 2026-07-28       1.1            zeh            补 bm_bus_impl.h include
 *                                                 （BM_BUS_DEFINE 已迁入内部头）
 *
 */
#include "balance_schedule.h"
#include "bm_bus.h"
#include "bm/core/bm_bus_impl.h"

/** 比例增益：cmd = -kp * pitch */
#define BALANCE_KP          0.5f
/** 调度 minor 拍粒度（us）：1ms；balance 每拍跑，telemetry 每 10 拍跑一次 */
#define CTRL_MINOR_US       1000u
/** 遥测滑动平均窗口权重（指数滑动平均：简单、零动态分配） */
#define TELEMETRY_EMA_ALPHA 0.2f

/* =========================================================================
 * 总线：imu_bus（输入，俯仰角）/ motor_bus（输出，力矩指令）/
 *      telemetry_bus（输出，聚合遥测值）
 * ========================================================================= */

BM_BUS_DEFINE(imu_bus, float, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(motor_bus, float, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(telemetry_bus, float, 4u, 1u, BM_BUS_LATEST);

bm_bus_t g_imu_bus;
bm_bus_t g_motor_bus;
bm_bus_t g_telemetry_bus;

static const float k_imu_safe = 0.0f;       /**< IMU STALE 兜底值：视为水平 */
static const float k_motor_safe = 0.0f;     /**< 电机 STALE 兜底值：零力矩，不出力 */
static const float k_telemetry_safe = 0.0f; /**< 遥测初始安全值 */

/** @brief telemetry 任务的自持状态：指数滑动平均累加器 */
typedef struct {
    float ema;
    int   initialized;
} telemetry_state_t;

static telemetry_state_t g_telemetry_state;

/* =========================================================================
 * balance（ISR 域）：简短的比例控制 step，读 IMU，写电机指令
 * ========================================================================= */

static const bm_let_input_t k_balance_inputs[] = {
    { .bus = &g_imu_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(float), .safe_default = &k_imu_safe },
};
static const bm_let_output_t k_balance_outputs[] = {
    { .bus = &g_motor_bus, .elem_size = sizeof(float),
      .safe_default = &k_motor_safe },
};

/** @brief ISR 域 step：cmd = -kp * pitch；pitch 为 STALE 时兜底为 0（不出力） */
static void balance_step(bm_let_ctx_t *ctx, void *state) {
    int stale;
    uint32_t age_us;
    const float *pitch;
    float *cmd;

    (void)state;
    pitch = (const float *)bm_let_in(ctx, 0u, &stale, &age_us);
    cmd = (float *)bm_let_out(ctx, 0u);

    if (stale) {
        *cmd = 0.0f; /* fail-safe：IMU 数据已过期/从未发布，不出力 */
    } else {
        *cmd = -BALANCE_KP * (*pitch);
    }
}

BM_LET_DEFINE_ISR(balance, 1u, 0u, 40u, balance_step, NULL,
                   k_balance_inputs, k_balance_outputs);

/* =========================================================================
 * telemetry（MAINLOOP 域）：每 10 拍对电机指令做一次指数滑动平均
 * ========================================================================= */

static const bm_let_input_t k_telemetry_inputs[] = {
    { .bus = &g_motor_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(float), .safe_default = &k_motor_safe },
};
static const bm_let_output_t k_telemetry_outputs[] = {
    { .bus = &g_telemetry_bus, .elem_size = sizeof(float),
      .safe_default = &k_telemetry_safe },
};

/** @brief MAINLOOP 域 step：较重任务样例——指数滑动平均，路由到主循环而非 ISR 时间片 */
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
 * 调度表
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
