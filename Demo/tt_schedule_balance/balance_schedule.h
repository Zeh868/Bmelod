/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file balance_schedule.h
 * @brief 平衡车 bm_tt_schedule 装配公共接口（ISR 力矩环 + MAINLOOP 遥测聚合）
 *
 * @details 声明 balance_schedule.c 中装配的 `ctrl` 调度表（任务 "balance"，
 * ISR 域；任务 "telemetry"，MAINLOOP 域）以及打开该表所有任务绑定总线的
 * setup 钩子。main.c 经本头文件加上 `bm_tt_schedule_init()` /
 * `bm_tt_schedule_tick()` / `bm_tt_schedule_run_pending()` 驱动本 demo；
 * 三个总线句柄也在此导出，是因为本 native_sim demo 没有真实
 * IMU/电机硬件——main.c 的 tick 循环直接注入合成 IMU 样本，并直接经这些
 * 总线读回 cmd/telemetry 以打印数据流。真实部署不需要这样：传感器/执行器
 * 会各自从自己的驱动代码里发布/订阅这些总线，而不是从 main() 里。
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
 *
 */
#ifndef BALANCE_SCHEDULE_H
#define BALANCE_SCHEDULE_H

#include "bm_tt_schedule.h"
#include "bm_bus.h"

/** @brief 本 demo 的调度表：minor=1000us，"balance"（ISR）+ "telemetry"（MAINLOOP） */
extern bm_tt_schedule_t ctrl;

/** @brief IMU 输入总线（俯仰角，弧度）；main.c 在其上注入合成样本 */
extern bm_bus_t g_imu_bus;
/** @brief 电机输出总线（力矩指令）；main.c 读回用于打印 */
extern bm_bus_t g_motor_bus;
/** @brief 遥测输出总线（滑动平均值）；main.c 读回用于打印 */
extern bm_bus_t g_telemetry_bus;

/**
 * @brief 打开 `ctrl` 调度表用到的全部总线
 *
 * @return 0 成功；任一 bm_bus_open() 调用失败则非 0
 */
int balance_schedule_setup(void);

#endif /* BALANCE_SCHEDULE_H */
