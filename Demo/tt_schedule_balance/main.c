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
 * @version 1.1
 * @date 2026-07-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-01       1.0            zeh            首个 bm_tt_schedule 平衡车场景 demo
 * 2026-07-03       1.1            zeh            拆分调度装配至 balance_schedule.c/.h
 *
 */
#include "balance_schedule.h"
#include "bm_bus.h"

#include <math.h>
#include <stdio.h>

/** 本 demo 手动驱动的拍数 */
#define DEMO_TICK_COUNT     30u
/** run_pending 每次最多跑几个待处理 MAINLOOP 任务（本 demo 只有一个够用） */
#define DEMO_RUN_PENDING_BUDGET 8u
/** IMU 断流演示窗口起始拍（用于触发 STALE fail-safe 降级路径演示） */
#define IMU_DROPOUT_TICK_START 14u
/** IMU 断流演示窗口拍数：max_age 默认 2×任务周期，需连续 ≥3 拍不发布才会
 *  真正触发 stale（miss×period > 2×period），本窗口留够余量 */
#define IMU_DROPOUT_TICK_COUNT 5u

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
    uint32_t tick;

    if (balance_schedule_setup() != 0) {
        fprintf(stderr, "balance_schedule_setup failed\n");
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
