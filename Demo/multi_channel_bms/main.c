/**
 * @file main.c
 * @brief 多通道 BMS 示例：Pack 采样 + 16 路电芯快照与过压检测
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-17       1.1            zeh            LPF 后 Pack 电压/SOC 简易指标（E1）
 *
 */
#include "app_bms.h"
#include "bm/algorithm/bm_algo_filter.h"
#include "bm/algorithm/bm_algo_common.h"
#include "bm_exec.h"
#include "bm_event.h"
#include "bm_module.h"
#include "bm_hrt.h"
#include "bm_log.h"
#include "bm_snapshot.h"
#include "bm_ticker.h"
#include "hybrid_print.h"

#include "bm_hal_adc_sim.h"
#include "hal/bm_hal_timer.h"
#include "hal/bm_hal_uart.h"

#ifdef NATIVE_SIM
#include "bm_hal_timer_native.h"
#endif
#ifdef BM_EXAMPLE_QEMU
#include "qemu_delay.h"
#endif

#include <stdint.h>

#define TAG "multi_bms"

/** 单电芯 OCV 线性 SOC 映射（E1 占位，非真实 OCV 表） */
#define CELL_SOC_V_MIN_MV   3000u
#define CELL_SOC_V_MAX_MV   4200u

#define PACK_ADC_KEY        100u
#define CELL_GPIO_BASE      200u

BM_SNAPSHOT_DEFINE(cell_voltage, uint16_t);

static cell_voltage_snapshot_t g_cell_voltages[CELL_COUNT];

typedef struct {
    uint32_t samples;
} pack_state_t;

typedef struct {
    uint32_t channel;
    uint32_t overvoltage_hits;
    uint16_t last_mv;
    uint16_t filtered_mv;
    bm_algo_lpf1_config_t lpf_cfg;
    bm_algo_lpf1_state_t  lpf;
    int      lpf_ready;
} cell_state_t;

static pack_state_t g_pack_state;
static cell_state_t g_cell_states[CELL_COUNT];

/** Pack 级 ADC 采样：遍历各电芯通道并发布快照 */
static void pack_sample_step(const bm_exec_t *instance) {
    pack_state_t *state = (pack_state_t *)instance->state;
    uint32_t ch;
    uint16_t value;

    for (ch = 0u; ch < CELL_COUNT; ++ch) {
        value = (uint16_t)(3300u + ch);
        bm_hal_adc_sim_set_rank(&BM_HAL_ADC_SIM0, ch, value);
        if (bm_hal_adc_read_injected(&BM_HAL_ADC_SIM0, ch, &value) == BM_OK) {
            BM_SNAPSHOT_PUBLISH(g_cell_voltages[ch], &value);
        }
    }
    state->samples++;
}

static int bind_pack_adc(const bm_exec_t *instance,
                         const bm_hal_hrt_binding_t *binding) {
    (void)instance;
    return bm_hal_adc_bind_complete(&BM_HAL_ADC_SIM0, binding);
}

static int pack_init(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

static int pack_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

static void pack_safe_stop(const bm_exec_t *instance) {
    (void)instance;
}

static const bm_exec_ops_t g_pack_ops = {
    pack_init, pack_start, pack_safe_stop
};

static const bm_exec_slot_t g_pack_slots[] = {
    {
        .kind = BM_EXEC_SLOT_HARDWARE,
        .run = pack_sample_step,
        .bind = bind_pack_adc,
        .name = "pack"
    }
};

static const bm_resource_claim_t g_pack_claim_table[] = {
    { BM_RESOURCE_CLASS_ADC_GROUP, PACK_ADC_KEY, BM_RESOURCE_OWNER, 1u, "adc_pack" }
};

static const bm_exec_t g_pack_sampler = {
    1u, "pack_sampler", &g_pack_state, NULL, NULL,
    g_pack_slots, 1u, g_pack_claim_table, 1u, &g_pack_ops
};

static int cell_init(const bm_exec_t *instance) {
    cell_state_t *state = (cell_state_t *)instance->state;
    float sample_hz;

    if (state == NULL) {
        return BM_ERR_INVALID;
    }
#if defined(BM_EXAMPLE_QEMU)
    sample_hz = 1000.0f / (float)10u;
#else
    sample_hz = 1000.0f / (float)100u;
#endif
    if (bm_algo_lpf1_init_from_cutoff(&state->lpf_cfg, 5.0f, sample_hz) != 0) {
        return BM_ERR_INVALID;
    }
    bm_algo_lpf1_reset(&state->lpf, 3300.0f);
    state->lpf_ready = 1;
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

static bm_resource_claim_t g_cell_claims[CELL_COUNT];
static bm_exec_t g_cells[CELL_COUNT];
static const bm_exec_t *g_instance_table[CELL_COUNT + 1u];

#if defined(BM_EXAMPLE_QEMU)
#define BMS_CHECK_MS  10u
#else
#define BMS_CHECK_MS  100u
#endif

static const bm_ticker_slot_t g_check_ticker[] = {
    { BMS_CHECK_MS, EVENT_CELL_CHECK, 1u, "cell_check" }
};

/** 周期性读取各电芯电压并检测过压 */
void app_on_cell_check(const bm_event_t *event, void *user_data) {
    uint32_t i;

    (void)user_data;
    if (event->type != EVENT_CELL_CHECK) {
        return;
    }

    for (i = 0u; i < CELL_COUNT; ++i) {
        uint16_t mv = 0u;
        float filtered;

        BM_SNAPSHOT_READ(g_cell_voltages[i], &mv);
        g_cell_states[i].last_mv = mv;
        if (g_cell_states[i].lpf_ready != 0) {
            filtered = bm_algo_lpf1_step(&g_cell_states[i].lpf,
                                         &g_cell_states[i].lpf_cfg,
                                         (float)mv);
            if (filtered < 0.0f) {
                filtered = 0.0f;
            }
            g_cell_states[i].filtered_mv = (uint16_t)filtered;
        } else {
            g_cell_states[i].filtered_mv = mv;
        }
        if (g_cell_states[i].filtered_mv > 4200u) {
            g_cell_states[i].overvoltage_hits++;
            BM_LOGW(TAG, "cell %u overvoltage: %u mV (raw %u)",
                    (unsigned)i,
                    (unsigned)g_cell_states[i].filtered_mv,
                    (unsigned)mv);
        }
    }
}

/** 动态构建 16 路电芯控制实例及资源声明 */
static void setup_cells(void) {
    uint32_t i;

    for (i = 0u; i < CELL_COUNT; ++i) {
        g_cell_claims[i].resource_class = BM_RESOURCE_CLASS_GPIO;
        g_cell_claims[i].key = CELL_GPIO_BASE + i;
        g_cell_claims[i].access = BM_RESOURCE_EXCLUSIVE;
        g_cell_claims[i].share_group = 0u;
        g_cell_claims[i].name = "cell_gpio";

        g_cell_states[i].channel = i;
        g_cells[i].id = 100u + i;
        g_cells[i].name = "cell";
        g_cells[i].state = &g_cell_states[i];
        g_cells[i].config = NULL;
        g_cells[i].resources = NULL;
        g_cells[i].slots = NULL;
        g_cells[i].slot_count = 0u;
        g_cells[i].claims = &g_cell_claims[i];
        g_cells[i].claim_count = 1u;
        g_cells[i].ops = &g_cell_ops;

        g_instance_table[i + 1u] = &g_cells[i];
    }
    g_instance_table[0] = &g_pack_sampler;
    BM_LOGD(TAG, "setup %u cell instances", (unsigned)CELL_COUNT);
}

/**
 * @brief 由滤波后电芯电压计算 Pack 级简易指标（E1 线性 SOC 占位）
 */
static void bms_report_pack_metrics(void) {
    uint32_t i;
    uint32_t sum_mv = 0u;
    uint16_t min_mv = 0xFFFFu;
    uint16_t max_mv = 0u;
    float avg_mv;
    float soc_est;

    for (i = 0u; i < CELL_COUNT; ++i) {
        uint16_t mv = g_cell_states[i].filtered_mv;

        sum_mv += (uint32_t)mv;
        if (mv < min_mv) {
            min_mv = mv;
        }
        if (mv > max_mv) {
            max_mv = mv;
        }
    }

    avg_mv = (float)sum_mv / (float)CELL_COUNT;
    soc_est = (avg_mv - (float)CELL_SOC_V_MIN_MV) /
              (float)(CELL_SOC_V_MAX_MV - CELL_SOC_V_MIN_MV);
    soc_est = bm_algo_clamp_f(soc_est, 0.0f, 1.0f);

    BM_LOGI(TAG,
            "pack metrics (E1 stub): avg=%u mV min=%u max=%u soc_est=%.1f%%",
            (unsigned)(uint16_t)avg_mv,
            (unsigned)min_mv,
            (unsigned)max_mv,
            (double)(soc_est * 100.0f));
}

int main(void) {
    uint32_t i;
    uint32_t reported = 0u;
    int rc;

    BM_LOGI(TAG, "multi_channel_bms example start");
    bm_hal_uart_init(NULL);
    setup_cells();

    rc = bm_module_boot();
    if (rc != BM_OK) {
        BM_LOGE(TAG, "module boot failed, rc=%d", rc);
        hybrid_print("EXAMPLE_MULTI_CHANNEL_BMS: FAIL boot\n");
        return 1;
    }

    (void)bm_hal_timer_init(1000u);

    rc = bm_exec_init_all(g_instance_table, CELL_COUNT + 1u);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "ctrl init failed, rc=%d", rc);
        hybrid_print("EXAMPLE_MULTI_CHANNEL_BMS: FAIL init\n");
        return 1;
    }
    rc = bm_exec_start_all(g_instance_table, CELL_COUNT + 1u);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "ctrl start failed, rc=%d", rc);
        bm_exec_safe_stop_all(g_instance_table, CELL_COUNT + 1u);
        return 1;
    }
#ifdef BM_EXAMPLE_QEMU
    bm_example_qemu_warmup();
#endif

    rc = bm_ticker_init(g_check_ticker, 1u);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "ticker init failed, rc=%d", rc);
        bm_exec_safe_stop_all(g_instance_table, CELL_COUNT + 1u);
        return 1;
    }

#ifdef NATIVE_SIM
    for (i = 0u; i < 500u; ++i) {
        bm_hal_timer_native_advance_ticks(1u);
        bm_hal_adc_sim_fire_complete(&BM_HAL_ADC_SIM0);
        (void)bm_ticker_poll();
        (void)bm_event_process(8u);
    }
#elif defined(BM_EXAMPLE_QEMU)
    for (i = 0u; i < 1500u; ++i) {
        bm_example_qemu_spin();
        bm_hal_adc_sim_fire_complete(&BM_HAL_ADC_SIM0);
        (void)bm_ticker_poll();
        (void)bm_event_process(8u);
    }
#else
    for (i = 0u; i < 200u; ++i) {
        for (volatile uint32_t d = 0u; d < 5000u; ++d) {
        }
        bm_hal_adc_sim_fire_complete(&BM_HAL_ADC_SIM0);
        (void)bm_ticker_poll();
        (void)bm_event_process(8u);
    }
#endif

    for (i = 0u; i < CELL_COUNT; ++i) {
        if (g_cell_states[i].last_mv >= 3300u) {
            reported++;
        }
    }

    if (g_pack_state.samples > 0u && reported == CELL_COUNT) {
        bms_report_pack_metrics();
        BM_LOGI(TAG, "metrics ok: samples=%u reported=%u",
                (unsigned)g_pack_state.samples, (unsigned)reported);
        hybrid_print_pass("MULTI_CHANNEL_BMS");
    } else {
        BM_LOGE(TAG, "metrics failed: samples=%u reported=%u/%u",
                (unsigned)g_pack_state.samples, (unsigned)reported,
                (unsigned)CELL_COUNT);
        hybrid_print("EXAMPLE_MULTI_CHANNEL_BMS: FAIL metrics\n");
        bm_exec_safe_stop_all(g_instance_table, CELL_COUNT + 1u);
        return 1;
    }

    bm_exec_safe_stop_all(g_instance_table, CELL_COUNT + 1u);
#ifdef NATIVE_SIM
    return 0;
#else
    while (1) {
    }
#endif
}
