/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file schedule_map_fixture.h
 * @brief schedule-map v2 宿主出表测试用的双表谐波装配件
 *
 * @details 声明在 schedule_map_fixture.c 中装配的两个 bm_tt_schedule_t
 * 实例（表 A：谐波三 ISR 任务 + 一个 MAINLOOP 任务；表 B：单个 ISR 任务），
 * 外加一个打开它们全部总线的 setup 钩子。由"正例"出表目标
 * （bm_schedule_map_dump）经 schedule_map_fixture_reg.c 使用。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-03
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-03       1.0            zeh            Task 3：双表装配件初版
 *
 */
#ifndef SCHEDULE_MAP_FIXTURE_H
#define SCHEDULE_MAP_FIXTURE_H

#include "bm_tt_schedule.h"

/** @brief 表 A：minor=1000us，谐波 fast/mid/slow（ISR）+ tele（MAINLOOP） */
extern bm_tt_schedule_t sched_fixture_a;
/** @brief 表 B：minor=2000us，单个 ISR 任务 "solo" */
extern bm_tt_schedule_t sched_fixture_b;

/**
 * @brief 打开 sched_fixture_a/sched_fixture_b 用到的全部总线
 *
 * @return 0 成功；任一 bm_bus_open() 失败则非 0
 */
int schedule_map_fixture_setup(void);

#endif /* SCHEDULE_MAP_FIXTURE_H */
