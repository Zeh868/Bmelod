/**
 * @file bm_mp_wdg.c
 * SPDX-License-Identifier: LicenseRef-Bmeflod-Proprietary
 * @brief 多核 heartbeat 聚合监督实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 *
 */
#include "bm/mp/bm_mp_wdg.h"
#include "bm/mp/bm_mp_ipc.h"
#include "bm/mp/bm_mp_cpu.h"
#include "bm/common/bm_atomic_ipc.h"
#include "bm_log.h"
#include "hal/bm_hal_cpu.h"
#include "hal/bm_hal_timer.h"

#include <string.h>

static bm_mp_wdg_safe_stop_fn_t s_safe_stop_hook;
static uint32_t s_last_hb[BM_CONFIG_CPU_COUNT];
static uint32_t s_last_change_ticks[BM_CONFIG_CPU_COUNT];
static uint8_t s_seen[BM_CONFIG_CPU_COUNT];
static bm_atomic_ipc_u32_t s_stopped;

void bm_mp_wdg_reset(void) {
    memset(s_last_hb, 0, sizeof(s_last_hb));
    memset(s_last_change_ticks, 0, sizeof(s_last_change_ticks));
    memset(s_seen, 0, sizeof(s_seen));
    bm_atomic_ipc_store_u32(&s_stopped, 0u);
}

void bm_mp_wdg_set_safe_stop_hook(bm_mp_wdg_safe_stop_fn_t hook) {
    s_safe_stop_hook = hook;
}

void bm_mp_wdg_feed_this_cpu(void) {
    bm_mp_ipc_matrix_t *matrix = bm_mp_ipc_matrix();
    uint32_t cpu = BM_CPU_THIS();

    if (!matrix || cpu >= BM_CONFIG_CPU_COUNT) {
        return;
    }
    bm_atomic_ipc_inc_u32(&matrix->cpu_hb_seq[cpu]);
}

int bm_mp_wdg_bootstrap_check(void) {
#if BM_CONFIG_CPU_COUNT <= 1u
    return BM_OK;
#else
    bm_mp_ipc_matrix_t *matrix = bm_mp_ipc_matrix();
    uint32_t now;
    uint32_t freq;
    uint64_t timeout_ticks;
    uint32_t cpu;
    int stalled = 0;

    if (!bm_hal_cpu_is_bootstrap()) {
        return BM_ERR_INVALID;
    }
    if (!matrix) {
        return BM_OK;
    }
    freq = bm_hal_timer_get_freq();
    if (freq == 0u) {
        return BM_ERR_NOT_INIT;
    }
    timeout_ticks =
        ((uint64_t)BM_CONFIG_MP_WDG_HB_TIMEOUT_MS * (uint64_t)freq +
         999ull) / 1000ull;
    if (timeout_ticks == 0u) {
        timeout_ticks = 1u;
    }
    if (timeout_ticks > UINT32_MAX) {
        return BM_ERR_OVERFLOW;
    }
    now = bm_hal_timer_get_ticks();

    for (cpu = 0u; cpu < BM_CONFIG_CPU_COUNT; cpu++) {
        uint32_t hb = bm_atomic_ipc_load_u32(&matrix->cpu_hb_seq[cpu]);

        /* 能执行到硬件喂狗路径即证明 Bootstrap 仍在推进。 */
        if (cpu == (uint32_t)BM_CPU_THIS()) {
            s_last_hb[cpu] = hb;
            continue;
        }
        if (!s_seen[cpu] || hb != s_last_hb[cpu]) {
            /*
             * 首次观测或序号变化：重置停滞计时。
             * 不要求每核 feed 频率一致，只要求序号 eventually 递增。
             */
            s_seen[cpu] = 1u;
            s_last_hb[cpu] = hb;
            s_last_change_ticks[cpu] = now;
            continue;
        }
        if ((uint32_t)(now - s_last_change_ticks[cpu]) >=
            (uint32_t)timeout_ticks) {
            BM_LOGE("mp_wdg", "cpu %u heartbeat stalled hb=%u",
                    (unsigned)cpu, (unsigned)hb);
            stalled = 1;
        }
    }
    if (stalled) {
        /*
         * 原子 test-and-set：仅第一个将 s_stopped 从 0 翻为 1 的调用者
         * 触发 safe-stop 钩子，避免多核同时检测到停滞时重复触发。
         */
        uint32_t was_stopped = bm_atomic_ipc_exchange_u32(&s_stopped, 1u);
        if (!was_stopped) {
            if (s_safe_stop_hook) {
                s_safe_stop_hook();
            }
        }
        return BM_ERR_TIMEOUT;
    }
    return BM_OK;
#endif
}
