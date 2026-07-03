/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file schedule_map_fixture.h
 * @brief Two-table harmonic fixture for schedule-map v2 host-dump tests
 *
 * @details Declares two bm_tt_schedule_t instances assembled in
 * schedule_map_fixture.c (table A: harmonic three ISR tasks + one MAINLOOP
 * task; table B: a single ISR task) plus a setup hook that opens all their
 * buses. Used by the "good" dump target (bm_schedule_map_dump) via
 * schedule_map_fixture_reg.c.
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-03
 *
 * @par Change log:
 *
 *    Date         Version        Author          Description
 * 2026-07-03       1.0            zeh            Task 3: two-table fixture
 *
 */
#ifndef SCHEDULE_MAP_FIXTURE_H
#define SCHEDULE_MAP_FIXTURE_H

#include "bm_tt_schedule.h"

/** @brief Table A: minor=1000us, harmonic fast/mid/slow (ISR) + tele (MAINLOOP) */
extern bm_tt_schedule_t sched_fixture_a;
/** @brief Table B: minor=2000us, single ISR task "solo" */
extern bm_tt_schedule_t sched_fixture_b;

/**
 * @brief Open every bus used by sched_fixture_a/sched_fixture_b
 *
 * @return 0 on success; non-zero on any bm_bus_open() failure
 */
int schedule_map_fixture_setup(void);

#endif /* SCHEDULE_MAP_FIXTURE_H */
