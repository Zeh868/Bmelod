/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file main.c
 * @brief bm_tt_schedule 平衡车 Demo：ISR 域力矩环 + MAINLOOP 域遥测聚合
 *
 * 演示 `BM_LET_DEFINE_ISR`/`BM_LET_DEFINE_MAINLOOP` 两个具名声明宏的典型
 * 用法，贴近真实平衡车场景（简化）：
 *   - `balance`（ISR 域）：每拍读 IMU 俯仰角 `pitch`，算比例力矩
 *     `cmd = -kp * pitch` 写给电机 bus；IMU 数据 STALE（从未发布/超期）时
 *     降级输出安全值 `cmd = 0`（不出力）。
 *   - `telemetry`（MAINLOOP 域）：每 10 拍对电机指令做一次滑动平均聚合，
 *     演示"重计算/非硬实时任务放主循环、不占 ISR 时间片"的接线方式。
 *
 * 本 demo 跑在 native 主机上，没有真实中断/HRT 硬件，因此用一个 for 循环
 * 手动模拟节拍驱动（每圈相当于一次 ISR tick + 一次主循环 run_pending），
 * 便于在没有硬件的环境下观察数据流与 +1 拍调度延迟现象。真实部署方式见
 * `main()` 中的注释块。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-01       1.0            zeh            首个 bm_tt_schedule 平衡车场景 demo
 *
 */
#include "bm_tt_schedule.h"
#include "bm_bus.h"

#include <math.h>
#include <stdio.h>

/** 比例增益：cmd = -kp * pitch */
#define BALANCE_KP          0.5f
/** 调度节拍粒度（µs）：1ms，balance 每拍跑一次，telemetry 每 10 拍跑一次 */
#define CTRL_MINOR_US       1000u
/** 本 demo 手动驱动的拍数 */
#define DEMO_TICK_COUNT     30u
/** run_pending 每次最多跑几个待处理 MAINLOOP 任务（本 demo 只有一个够用） */
#define DEMO_RUN_PENDING_BUDGET 8u
/** 遥测滑动平均窗口权重（指数滑动平均，简单、零动态分配） */
#define TELEMETRY_EMA_ALPHA 0.2f
/** IMU 断流演示窗口起始拍（用于触发 STALE fail-safe 降级路径演示） */
#define IMU_DROPOUT_TICK_START 14u
/** IMU 断流演示窗口拍数：max_age 默认 2×任务周期，需连续 ≥3 拍不发布才会
 *  真正触发 stale（miss×period > 2×period），本窗口留够余量 */
#define IMU_DROPOUT_TICK_COUNT 5u

/* =========================================================================
 * 总线：imu_bus（输入，pitch 角）/ motor_bus（输出，力矩 cmd）/
 *       telemetry_bus（输出，遥测聚合值）
 * ========================================================================= */

BM_BUS_DEFINE(imu_bus, float, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(motor_bus, float, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(telemetry_bus, float, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_imu_bus;
static bm_bus_t g_motor_bus;
static bm_bus_t g_telemetry_bus;

static const float k_imu_safe = 0.0f;       /**< IMU STALE 兜底：视为水平 */
static const float k_motor_safe = 0.0f;     /**< 电机 STALE 兜底：零力矩，不出力 */
static const float k_telemetry_safe = 0.0f; /**< 遥测初始安全值 */

/** @brief telemetry 任务的自持状态：指数滑动平均累计值 */
typedef struct {
    float ema;
    int   initialized;
} telemetry_state_t;

static telemetry_state_t g_telemetry_state;

/* =========================================================================
 * balance（ISR 域）：短小的比例控制 step，读 IMU、写电机指令
 * ========================================================================= */

static const bm_let_input_t k_balance_inputs[] = {
    { .bus = &g_imu_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(float), .safe_default = &k_imu_safe },
};
static const bm_let_output_t k_balance_outputs[] = {
    { .bus = &g_motor_bus, .elem_size = sizeof(float),
      .safe_default = &k_motor_safe },
};

/** @brief ISR 域 step：cmd = -kp * pitch；pitch STALE 时降级输出 0（不出力） */
static void balance_step(bm_let_ctx_t *ctx, void *state) {
    int stale;
    uint32_t age_us;
    const float *pitch;
    float *cmd;

    (void)state;
    pitch = (const float *)bm_let_in(ctx, 0u, &stale, &age_us);
    cmd = (float *)bm_let_out(ctx, 0u);

    if (stale) {
        *cmd = 0.0f; /* fail-safe：IMU 数据过期/从未发布，不出力 */
    } else {
        *cmd = -BALANCE_KP * (*pitch);
    }
}

BM_LET_DEFINE_ISR(balance, 1u, 0u, 40u, balance_step, NULL,
                   k_balance_inputs, k_balance_outputs);

/* =========================================================================
 * telemetry（MAINLOOP 域）：每 10 拍对电机指令做一次指数滑动平均聚合
 * ========================================================================= */

static const bm_let_input_t k_telemetry_inputs[] = {
    { .bus = &g_motor_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(float), .safe_default = &k_motor_safe },
};
static const bm_let_output_t k_telemetry_outputs[] = {
    { .bus = &g_telemetry_bus, .elem_size = sizeof(float),
      .safe_default = &k_telemetry_safe },
};

/** @brief MAINLOOP 域 step：重任务示例——指数滑动平均聚合，放主循环不占 ISR 时间片 */
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

/** @brief bm_tt_schedule_report 的 emit 回调：逐行打印到 stdout */
static void print_line(const char *line, void *u) {
    (void)u;
    printf("%s\n", line);
}

/** @brief 小发布助手：acquire_write → 写值 → commit（LATEST 无单调用 write API） */
static int publish_f32(bm_bus_t *h, float v) {
    void *slot;
    int rc = bm_bus_acquire_write(h, &slot);

    if (rc != BM_OK) {
        return rc;
    }
    *(float *)slot = v;
    return bm_bus_commit(h);
}

int main(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    uint32_t tick;

    if (bm_bus_open(&g_imu_bus, &imu_bus_storage, &cfg) != BM_OK ||
        bm_bus_open(&g_motor_bus, &motor_bus_storage, &cfg) != BM_OK ||
        bm_bus_open(&g_telemetry_bus, &telemetry_bus_storage, &cfg) != BM_OK) {
        fprintf(stderr, "bm_bus_open failed\n");
        return 1;
    }

    if (bm_tt_schedule_init(&ctrl) != BM_OK) {
        fprintf(stderr, "bm_tt_schedule_init failed\n");
        return 1;
    }

    /*
     * 真实部署方式（真机，非本 demo 路径）：
     *   bm_hrt_slot_t slots[1];
     *   slots[0] = bm_tt_schedule_hrt_slot(&ctrl);   // period_us = minor_us
     *   bm_hrt_init(slots, 1u);                      // 并入 hrt slot 表
     *   bm_hrt_start();                               // ISR 定时触发 bm_tt_schedule_tick
     *   // 主循环周期性调用（与 bm_exec_drain_streams/bm_event_process 并列）：
     *   for (;;) {
     *       (void)bm_tt_schedule_run_pending(&ctrl, budget);
     *       ...
     *   }
     * 本 demo 跑在 native 主机、无硬件 HRT，改用下面的 for 循环手动驱动
     * tick + run_pending，模拟同样的节拍节奏，便于在无硬件环境下观察数据流。
     */

    printf("=== bm_tt_schedule balance demo: schedule overview ===\n");
    bm_tt_schedule_report(&ctrl, print_line, NULL);
    printf("=== tick loop (N=%u) ===\n", (unsigned)DEMO_TICK_COUNT);

    for (tick = 0u; tick < DEMO_TICK_COUNT; ++tick) {
        float pitch;
        float cmd;
        float telem;
        int cmd_rc;
        int telem_rc;
        int dropout;

        /* 模拟 IMU 采样：一段随拍数变化的俯仰角（弧度），幅值 ±0.2rad */
        pitch = 0.2f * sinf((float)tick * 0.3f);

        /* tick∈[IMU_DROPOUT_TICK_START, +IMU_DROPOUT_TICK_COUNT) 故意不发布新值，
         * 模拟 IMU 短暂断流：baseline_seq 连续多拍不变 → miss 累加 → age 超
         * 2×任务周期 → stale=1，balance_step 走 fail-safe 降级输出 cmd=0，
         * 而非假装数据仍新鲜继续用旧俯仰角算力矩。*/
        dropout = (tick >= IMU_DROPOUT_TICK_START &&
                   tick < IMU_DROPOUT_TICK_START + IMU_DROPOUT_TICK_COUNT);
        if (!dropout) {
            if (publish_f32(&g_imu_bus, pitch) != BM_OK) {
                fprintf(stderr, "publish imu failed at tick %u\n", (unsigned)tick);
                return 1;
            }
        }

        bm_tt_schedule_tick(&ctrl);
        (void)bm_tt_schedule_run_pending(&ctrl, DEMO_RUN_PENDING_BUDGET);

        cmd_rc = bm_bus_latest_read(&g_motor_bus, &cmd);
        telem_rc = bm_bus_latest_read(&g_telemetry_bus, &telem);

        printf("tick=%2u pitch=%+.4f%s cmd=%+.4f%s", (unsigned)tick,
               (double)pitch, dropout ? "(dropout,not-published)" : "",
               cmd_rc == BM_OK ? (double)cmd : 0.0,
               cmd_rc == BM_OK ? "" : " (motor: no data)");
        if (telem_rc == BM_OK) {
            printf(" telemetry_avg=%+.4f", (double)telem);
        }
        printf("\n");
    }

    printf("=== demo done ===\n");
    return 0;
}
