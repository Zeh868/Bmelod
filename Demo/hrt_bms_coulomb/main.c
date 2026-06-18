/**
 * @file main.c
 * @brief HRT BMS 库仑计示例：Pack 电流采样 + 电芯积分 + 比较器安全停机
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 *
 */
#include "app_bms.h"
#include "bms_demo_shared.h"
#include "bm/algorithm/bm_algo_battery.h"
#include "bm_exec.h"
#include "bm_hrt.h"
#include "bm_log.h"
#include "bm_module.h"
#include "bm_snapshot.h"
#include "bm_ticker.h"
#include "hybrid_print.h"

#include "bm_hal_adc_sim.h"
#include "bm_hal_comp_sim.h"
#include "bm_hal_pwm_sim.h"
#include "hal/bm_hal_timer.h"
#include "hal/bm_hal_uart.h"

#ifdef NATIVE_SIM
#include "bm_hal_timer_native.h"
#endif
#ifdef BM_EXAMPLE_QEMU
#include "qemu_delay.h"
#endif

#include <stdint.h>

#define TAG "hrt_bms"

BM_SNAPSHOT_DEFINE(pack_current, float);

static pack_current_snapshot_t g_pack_current = BM_SNAPSHOT_INITIALIZER;

typedef struct {
    uint32_t sample_count;
} pack_state_t;

typedef struct {
    bm_algo_coulomb_state_t  coulomb;
    bm_algo_coulomb_config_t coulomb_cfg;
    uint32_t integrate_ticks;
} cell_state_t;

static pack_state_t g_pack_state;
static cell_state_t g_cell_state;

/** Pack 电流采样：ADC 读数换算为安培并发布快照 */
static void pack_sample_step(const bm_exec_t *instance) {
    pack_state_t *state = (pack_state_t *)instance->state;
    uint16_t raw = 0u;
    float amps;

    if (bm_hal_adc_read_injected(&BM_HAL_ADC_SIM0, 0u, &raw) != BM_OK) {
        return;
    }

    amps = (float)raw / 4096.0f;
    BM_SNAPSHOT_PUBLISH(g_pack_current, &amps);
    state->sample_count++;
}

static int bind_pack_adc(const bm_exec_t *instance,
                         const bm_hal_hrt_binding_t *binding) {
    (void)instance;
    return bm_hal_adc_bind_complete(&BM_HAL_ADC_SIM0, binding);
}

static int pack_init(const bm_exec_t *instance) {
    (void)instance;
    bm_hal_adc_sim_set_rank(&BM_HAL_ADC_SIM0, 0u, 4095u);
    (void)bm_hal_pwm_enable_outputs(&BM_HAL_PWM_SIM0, 1);
    BM_LOGD(TAG, "pack init, ADC full-scale, PWM enabled");
    return BM_OK;
}

static int pack_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

static void pack_safe_stop(const bm_exec_t *instance) {
    (void)instance;
    BM_LOGW(TAG, "pack safe stop");
    bm_hal_pwm_request_safe_state(&BM_HAL_PWM_SIM0);
}

static const bm_exec_ops_t g_pack_ops = {
    pack_init, pack_start, pack_safe_stop
};

static const bm_exec_slot_t g_pack_slots[] = {
    {
        .kind = BM_EXEC_SLOT_HARDWARE,
        .run = pack_sample_step,
        .bind = bind_pack_adc,
        .name = "pack_sample"
    }
};

static const bm_exec_t g_pack_sampler = {
    10u, "pack_sampler", &g_pack_state, NULL, NULL,
    g_pack_slots, 1u, NULL, 0u, &g_pack_ops
};

#if defined(BM_EXAMPLE_QEMU)
#define CELL_TICKER_MS  10u
#else
#define CELL_TICKER_MS  100u
#endif

#define CELL_DT_S  ((float)CELL_TICKER_MS / 1000.0f)

/** 电芯库仑积分：读取 Pack 电流快照，经 bm_algo_coulomb_step 更新 SOC */
void app_cell_integrate_step(const bm_exec_t *instance) {
    cell_state_t *state = (cell_state_t *)instance->state;
    float amps;

    BM_SNAPSHOT_READ(g_pack_current, &amps);
    (void)bm_algo_coulomb_step(&state->coulomb, &state->coulomb_cfg,
                                amps, CELL_DT_S);
    state->integrate_ticks++;
}

static int cell_init(const bm_exec_t *instance) {
    cell_state_t *state = (cell_state_t *)instance->state;

    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    bms_demo_coulomb_config_init(&state->coulomb_cfg);
    bm_algo_coulomb_reset(&state->coulomb, BMS_DEMO_SOC_INIT);
    return BM_OK;
}

static int cell_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

static void cell_safe_stop(const bm_exec_t *instance) {
    (void)instance;
}

static const bm_exec_ops_t g_cell_ops = {
    cell_init, cell_start, cell_safe_stop
};

static const bm_ticker_slot_t g_cell_ticker[] = {
    { CELL_TICKER_MS, TICKER_INTEGRATE, 1u, "integrate" }
};

const bm_exec_t g_cell_0 = {
    20u, "cell_0", &g_cell_state, NULL, NULL,
    NULL, 0u, NULL, 0u, &g_cell_ops
};

static const bm_exec_t *const g_instances[] = {
    &g_pack_sampler,
    &g_cell_0
};

/** 推进仿真并轮询 ticker / 事件 */
static void run_sim(uint32_t cycles) {
    uint32_t i;

    for (i = 0u; i < cycles; ++i) {
#ifdef NATIVE_SIM
        bm_hal_timer_native_advance_ticks(1u);
#elif defined(BM_EXAMPLE_QEMU)
        bm_example_qemu_spin();
#else
        for (volatile uint32_t d = 0u; d < 20u; ++d) {
        }
#endif
        if ((i % 1u) == 0u) {
            bm_hal_adc_sim_fire_complete(&BM_HAL_ADC_SIM0);
        }
        (void)bm_ticker_poll();
        (void)bm_event_process(4u);
    }
}

int main(void) {
    int rc;

    BM_LOGI(TAG, "hrt_bms_coulomb example start");
    bm_hal_uart_init(NULL);

    rc = bm_module_boot();
    if (rc != BM_OK) {
        BM_LOGE(TAG, "module boot failed, rc=%d", rc);
        hybrid_print("EXAMPLE_HRT_BMS_COULOMB: FAIL boot\n");
        return 1;
    }

    (void)bm_hal_timer_init(1000u);
    bm_hal_comp_sim_set_threshold(&BM_HAL_COMP_SIM0, 3000u);

    rc = bm_exec_init_all(g_instances, 2u);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "ctrl init failed, rc=%d", rc);
        hybrid_print("EXAMPLE_HRT_BMS_COULOMB: FAIL init\n");
        return 1;
    }
    rc = bm_exec_start_all(g_instances, 2u);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "ctrl start failed, rc=%d", rc);
        bm_exec_safe_stop_all(g_instances, 2u);
        return 1;
    }
#ifdef BM_EXAMPLE_QEMU
    bm_example_qemu_warmup();
#endif

    rc = bm_ticker_init(g_cell_ticker, 1u);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "ticker init failed, rc=%d", rc);
        bm_exec_safe_stop_all(g_instances, 2u);
        return 1;
    }

#ifdef NATIVE_SIM
    run_sim(1000u);
#elif defined(BM_EXAMPLE_QEMU)
    run_sim(2000u);
#else
    run_sim(500u);
#endif

    /* 模拟比较器过压触发，请求 PWM 安全态 */
    bm_hal_comp_sim_trip(&BM_HAL_COMP_SIM0, 4000u);
    bm_hal_pwm_request_safe_state(&BM_HAL_PWM_SIM0);
    BM_LOGW(TAG, "comparator tripped, PWM safe state requested");

    if (g_pack_state.sample_count > 0u &&
        g_cell_state.integrate_ticks > 0u &&
        g_cell_state.coulomb.soc > BMS_DEMO_SOC_INIT &&
        !bm_hal_pwm_sim_outputs_enabled(&BM_HAL_PWM_SIM0)) {
        BM_LOGI(TAG, "metrics ok: samples=%u integrate=%u soc=%.4f",
                (unsigned)g_pack_state.sample_count,
                (unsigned)g_cell_state.integrate_ticks,
                (double)g_cell_state.coulomb.soc);
        hybrid_print_pass("HRT_BMS_COULOMB");
    } else {
        BM_LOGE(TAG, "metrics failed: samples=%u integrate=%u soc=%.4f pwm=%d",
                (unsigned)g_pack_state.sample_count,
                (unsigned)g_cell_state.integrate_ticks,
                (double)g_cell_state.coulomb.soc,
                bm_hal_pwm_sim_outputs_enabled(&BM_HAL_PWM_SIM0));
        hybrid_print("EXAMPLE_HRT_BMS_COULOMB: FAIL metrics\n");
        bm_exec_safe_stop_all(g_instances, 2u);
        return 1;
    }

    bm_exec_safe_stop_all(g_instances, 2u);
#ifdef NATIVE_SIM
    return 0;
#else
    while (1) {
    }
#endif
}
