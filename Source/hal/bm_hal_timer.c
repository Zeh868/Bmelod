/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_timer.c
 * @brief 瀹氭椂鍣?HAL 鍒嗗彂灞傦紙濂戠害 鈫?driver API锛?
 *
 * 鏈?BM_DRV_HAS_BACKEND 鏃惰浆鍙戣嚦 Port driver API锛涘惁鍒欐彁渚涙々瀹炵幇銆? * native_sim 璺緞鍙粡 BM_NATIVE_SIM_TIMER_CPU_LOCAL 鍚敤杞欢瀹氭椂鍣ㄣ€? * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-06-15
 *
 * @par 淇敼鏃ュ織:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            姝ｅ紡鍙戝竷
 * 2026-06-15       1.1            zeh            闈炴硶 CPU timer handle fail-closed
 * 2026-06-15       1.2            zeh            闈炴硶 CPU epoch 浣跨敤鏃犳晥鍝ㄥ叺
 *
 */
#include "bm_drv_timer.h"
#include "bm_hal_timer.h"
#include "bm/hybrid/bm_timestamp.h"
#include "bm/core/bm_cpu_local.h"
#include "bm_types.h"
#include "hal/bm_hal_cache.h"
#include "bm/common/bm_atomic_ipc.h"

#if defined(BM_NATIVE_SIM_TIMER_CPU_LOCAL)
#include "bm_hal_timer_native.h"
#endif

#ifdef BM_DRV_HAS_BACKEND
extern const struct bm_timer_driver_api bm_drv_timer_api;
#define BM_TIMER_DRV (&bm_drv_timer_api)
#else
static int timer_stub_init(uint32_t freq_hz) {
    (void)freq_hz;
    return BM_ERR_NOT_INIT;
}

static void timer_stub_stop(void) {
}

static uint32_t timer_stub_get_ticks(void) {
    return 0u;
}

static uint32_t timer_stub_get_freq(void) {
    return 0u;
}

static void timer_stub_set_callback(void (*cb)(void)) {
    (void)cb;
}

static const struct bm_timer_driver_api timer_stub = {
    timer_stub_init,
    timer_stub_stop,
    timer_stub_get_ticks,
    timer_stub_get_freq,
    timer_stub_set_callback,
};

#define BM_TIMER_DRV (&timer_stub)
#endif

/*
 * CPU 缁村害鐨勬椂閽?epoch 鏁扮粍鈥斺€旀寜 CPU 璺敱鏃堕』淇濊瘉鍙鎬э細
 * - 缂撳瓨涓€鑷存€х‖浠讹細release/acquire 閰嶅鍗宠冻澶熴€? * - 闈炰竴鑷寸紦瀛樺钩鍙帮細鏈々鍦ㄩ潪 NOOP D-cache 骞冲彴棰濆鎵ц
 *   cache clean/invalidate锛涗粛寮虹儓寤鸿灏?epoch 缃簬涓嶅彲缂撳瓨 SRAM
 *   锛圡PU/MMU 灞炴€э級浠ヨ幏寰楃‘瀹氭€?WCET銆? */
static bm_atomic_ipc_u32_t s_clock_epoch[BM_CONFIG_CPU_COUNT];

int bm_hal_timer_init(uint32_t freq_hz) {
    if (!BM_TIMER_DRV->init) {
        return BM_ERR_NOT_INIT;
    }
    return BM_TIMER_DRV->init(freq_hz);
}

void bm_hal_timer_stop(void) {
    if (BM_TIMER_DRV->stop) {
        BM_TIMER_DRV->stop();
    }
}

uint32_t bm_hal_timer_get_ticks(void) {
    if (!BM_TIMER_DRV->get_ticks) {
        return 0u;
    }
    return BM_TIMER_DRV->get_ticks();
}

uint32_t bm_hal_timer_get_freq(void) {
    if (!BM_TIMER_DRV->get_freq) {
        return 0u;
    }
    return BM_TIMER_DRV->get_freq();
}

void bm_hal_timer_set_callback(void (*cb)(void)) {
    if (BM_TIMER_DRV->set_callback) {
        BM_TIMER_DRV->set_callback(cb);
    }
}

uint16_t bm_hal_timer_clock_id_for_cpu(uint32_t cpu) {
    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return UINT16_MAX;
    }
    /*
     * 濮旀墭 bm_timestamp_clock_for_cpu() 鎵ц瀹為檯鏄犲皠锛?
     * 閬垮厤涓ゅ閲嶅缁存姢 clock_id 鍒嗛厤閫昏緫銆?
     */
    return bm_timestamp_clock_for_cpu(cpu);
}

uint32_t bm_hal_timer_get_ticks_on_cpu(uint32_t cpu) {
#if BM_CPU_LOCAL_ENABLE_ROUTE
    if (cpu == BM_CPU_THIS()) {
        return bm_hal_timer_get_ticks();
    }
#if defined(BM_NATIVE_SIM_TIMER_CPU_LOCAL)
    return bm_hal_timer_native_ticks_on_cpu(cpu);
#else
    (void)cpu;
    return 0u;
#endif
#else
    (void)cpu;
    return bm_hal_timer_get_ticks();
#endif
}
uint32_t bm_hal_timer_get_freq_on_cpu(uint32_t cpu) {
#if BM_CPU_LOCAL_ENABLE_ROUTE
    if (cpu == BM_CPU_THIS()) {
        return bm_hal_timer_get_freq();
    }
#if defined(BM_NATIVE_SIM_TIMER_CPU_LOCAL)
    return bm_hal_timer_native_freq_on_cpu(cpu);
#else
    (void)cpu;
    return 0u;
#endif
#else
    (void)cpu;
    return bm_hal_timer_get_freq();
#endif
}

uint32_t bm_hal_timer_clock_epoch_for_cpu(uint32_t cpu) {
    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return UINT32_MAX;
    }
    /*
     * 闈炰竴鑷寸紦瀛樺钩鍙帮細璇诲彇鍓?invalidate锛岀‘淇濊鍒版渶鏂?epoch銆?     * 鍦ㄧ紦瀛樹竴鑷存€х‖浠舵垨 NOOP 骞冲彴涓婏紝invalidate 閫€鍖栦负 acquire fence銆?     */
    if (!bm_hal_cache_is_noop()) {
        bm_hal_cache_invalidate(&s_clock_epoch[cpu],
                                (uint32_t)sizeof(s_clock_epoch[cpu]));
    }
    return bm_atomic_ipc_load_u32(&s_clock_epoch[cpu]);
}

bm_hal_timer_handle_t bm_hal_timer_for_cpu(uint32_t cpu) {
    bm_hal_timer_handle_t handle;

    if (cpu >= BM_CONFIG_CPU_COUNT) {
        handle.cpu = UINT8_MAX;
        handle.clock_id = UINT16_MAX;
        handle.clock_epoch = UINT32_MAX;
        return handle;
    }
    handle.cpu = (uint8_t)cpu;
    handle.clock_id = bm_hal_timer_clock_id_for_cpu(cpu);
    if (!bm_hal_cache_is_noop()) {
        bm_hal_cache_invalidate(&s_clock_epoch[cpu],
                                (uint32_t)sizeof(s_clock_epoch[cpu]));
    }
    handle.clock_epoch = bm_atomic_ipc_load_u32(&s_clock_epoch[cpu]);
    return handle;
}

void bm_hal_timer_bump_clock_epoch(uint32_t cpu) {
    if (cpu < BM_CONFIG_CPU_COUNT) {
        bm_atomic_ipc_inc_u32(&s_clock_epoch[cpu]);
        /*
         * 闈炰竴鑷寸紦瀛樺钩鍙帮細鑷鍚?clean锛屽皢鏂?epoch 鍐欏叆鍏变韩鍐呭瓨銆?         * 鍦ㄧ紦瀛樹竴鑷存€х‖浠舵垨 NOOP 骞冲彴涓婏紝clean 閫€鍖栦负 release fence銆?         */
        if (!bm_hal_cache_is_noop()) {
            bm_hal_cache_clean(&s_clock_epoch[cpu],
                               (uint32_t)sizeof(s_clock_epoch[cpu]));
        }
    }
}

void bm_hal_timer_bump_all_clock_epochs(void) {
    uint32_t cpu;

    for (cpu = 0u; cpu < BM_CONFIG_CPU_COUNT; cpu++) {
        bm_hal_timer_bump_clock_epoch(cpu);
    }
}
