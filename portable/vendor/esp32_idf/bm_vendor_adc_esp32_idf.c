/**
 * @file bm_vendor_adc_esp32_idf.c
 * @brief ESP32-WROOM-32E 裸机 ADC1 采样实现
 *
 * 本实现仅使用 ESP-IDF 的 ADC LL / SoC 寄存器头文件，不依赖驱动层、
 * FreeRTOS 任务或队列。采样由确定性的轮询路径触发，供 PWM 周期回调使用。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            新增双电机 ADC 实例
 * 2026-06-19       1.1            zeh          改为 ADC LL 裸机实现
 */
#include "bm_vendor_adc_esp32_idf.h"
#include "bm_vendor_esp32_idf_compat.h"
#include "bm_hal_instances_esp32wroom32e.h"
#include "bm_types.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(BM_ESP32_BAREMETAL)
#include "hal/adc_ll.h"
#include "esp_rom_sys.h"
#endif

/** @brief 电机实例数量。*/
#define BM_VENDOR_ADC_INSTANCE_COUNT  2u
/** @brief 单个实例的采样 rank 数。*/
#define BM_VENDOR_ADC_RANK_COUNT      2u
/** @brief 单次轮询超时上限。*/
#define BM_VENDOR_ADC_POLL_LIMIT      2048u

typedef struct {
    /** @brief 电机编号。*/
    uint32_t id;
} bm_vendor_adc_config_t;

typedef struct {
    /** @brief 是否完成硬件初始化。*/
    int initialized;
    /** @brief 缓存的原始 ADC 值。*/
    uint16_t cached[BM_VENDOR_ADC_RANK_COUNT];
    /** @brief HRT 完成回调绑定。*/
    bm_hal_hrt_binding_t complete_binding;
} bm_vendor_adc_context_t;

/** @brief M0 / M1 独立上下文。*/
static bm_vendor_adc_context_t g_adc_context[BM_VENDOR_ADC_INSTANCE_COUNT];

/** @brief M0 / M1 实例配置。*/
static const bm_vendor_adc_config_t g_adc_config[BM_VENDOR_ADC_INSTANCE_COUNT] = {
    { 0u },
    { 1u },
};

/** @brief M0 ADC1 通道。*/
static const int g_adc_channels_m0[BM_VENDOR_ADC_RANK_COUNT] = {
    3,
    0,
};

/** @brief M1 ADC1 通道。*/
static const int g_adc_channels_m1[BM_VENDOR_ADC_RANK_COUNT] = {
    7,
    6,
};

/**
 * @brief 从设备实例提取板级上下文。
 * @param dev HAL 设备实例。
 * @return 板级上下文；无效时返回 NULL。
 */
static bm_vendor_adc_context_t *bm_vendor_adc_context_for(const struct bm_hal_adc *dev) {
    const bm_vendor_adc_config_t *cfg;

    if (dev == NULL || dev->config == NULL) {
        return NULL;
    }
    cfg = (const bm_vendor_adc_config_t *)dev->config;
    if (cfg->id >= BM_VENDOR_ADC_INSTANCE_COUNT) {
        return NULL;
    }
    return &g_adc_context[cfg->id];
}

#if defined(BM_ESP32_BAREMETAL)
/**
 * @brief 初始化单个 ADC1 实例。
 * @param ctx 板级上下文。
 * @return BM_OK 成功；否则为平台错误码。
 */
static int bm_vendor_adc_hw_init(bm_vendor_adc_context_t *ctx) {
    const int *channels;
    uint32_t rank;

    if (ctx == NULL || ctx->initialized) {
        return BM_OK;
    }

    adc_ll_enable_bus_clock(true);
    adc_ll_reset_register();
    adc_ll_set_controller(ADC_UNIT_1, ADC_LL_CTRL_RTC);
    adc_oneshot_ll_set_output_bits(ADC_UNIT_1, ADC_BITWIDTH_12);
    adc_oneshot_ll_enable(ADC_UNIT_1);

    channels = (ctx == &g_adc_context[0]) ? g_adc_channels_m0 : g_adc_channels_m1;
    for (rank = 0u; rank < BM_VENDOR_ADC_RANK_COUNT; ++rank) {
        adc_oneshot_ll_set_atten(ADC_UNIT_1, (adc_channel_t)channels[rank], ADC_ATTEN_DB_11);
    }

    ctx->initialized = 1;
    return BM_OK;
}

/**
 * @brief 触发一次 ADC1 轮询并更新缓存。
 * @param ctx 板级上下文。
 * @return BM_OK 成功；否则为平台错误码。
 */
static int bm_vendor_adc_sample(bm_vendor_adc_context_t *ctx) {
    const int *channels;
    uint32_t rank;
    uint32_t wait;

    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_vendor_adc_hw_init(ctx) != BM_OK) {
        return BM_ERR_IO;
    }

    channels = (ctx == &g_adc_context[0]) ? g_adc_channels_m0 : g_adc_channels_m1;
    for (rank = 0u; rank < BM_VENDOR_ADC_RANK_COUNT; ++rank) {
        adc_oneshot_ll_set_channel(ADC_UNIT_1, channels[rank]);
        adc_oneshot_ll_start(ADC_UNIT_1);
        for (wait = 0u; wait < BM_VENDOR_ADC_POLL_LIMIT; ++wait) {
            if (adc_oneshot_ll_get_event(ADC_LL_EVENT_ADC1_ONESHOT_DONE)) {
                break;
            }
        }
        if (wait >= BM_VENDOR_ADC_POLL_LIMIT) {
            adc_oneshot_ll_disable_channel(ADC_UNIT_1);
            return BM_ERR_IO;
        }
        ctx->cached[rank] = (uint16_t)adc_oneshot_ll_get_raw_result(ADC_UNIT_1);
        adc_oneshot_ll_clear_event(ADC_LL_EVENT_ADC1_ONESHOT_DONE);
    }

    return BM_OK;
}
#endif

/**
 * @brief 读取单次电流采样并触发完成回调。
 * @param motor_id 电机编号。
 */
void bm_vendor_adc_esp32_idf_pwm_tick(uint32_t motor_id) {
    bm_vendor_adc_context_t *ctx;

    if (motor_id >= BM_VENDOR_ADC_INSTANCE_COUNT) {
        return;
    }
    ctx = &g_adc_context[motor_id];

#if defined(BM_ESP32_BAREMETAL)
    if (bm_vendor_adc_sample(ctx) != BM_OK) {
        return;
    }
#endif
    if (ctx->complete_binding.callback != NULL) {
        ctx->complete_binding.callback(ctx->complete_binding.context);
    }
}

/**
 * @brief 读取缓存的注入通道值。
 * @param dev HAL 设备实例。
 * @param rank 采样序号。
 * @param value 输出值。
 * @return BM_OK 成功；否则为错误码。
 */
static int bm_vendor_adc_read_injected(const struct bm_hal_adc *dev,
                                       uint32_t rank, uint16_t *value) {
    bm_vendor_adc_context_t *ctx;

    if (value == NULL || rank >= BM_VENDOR_ADC_RANK_COUNT) {
        return BM_ERR_INVALID;
    }
    ctx = bm_vendor_adc_context_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }

#if defined(BM_ESP32_BAREMETAL)
    if (bm_vendor_adc_hw_init(ctx) != BM_OK) {
        return BM_ERR_IO;
    }
#endif
    *value = ctx->cached[rank];
    return BM_OK;
}

/**
 * @brief 绑定采样完成回调。
 * @param dev HAL 设备实例。
 * @param binding 回调绑定。
 * @return BM_OK 成功；否则为错误码。
 */
static int bm_vendor_adc_bind_complete(const struct bm_hal_adc *dev,
                                       const bm_hal_hrt_binding_t *binding) {
    bm_vendor_adc_context_t *ctx;

    ctx = bm_vendor_adc_context_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    if (binding == NULL) {
        memset(&ctx->complete_binding, 0, sizeof(ctx->complete_binding));
        return BM_OK;
    }
    ctx->complete_binding = *binding;
    return BM_OK;
}

/** @brief ADC 驱动 API 表。*/
static const struct bm_adc_driver_api g_adc_api = {
    bm_vendor_adc_read_injected,
    bm_vendor_adc_bind_complete,
};

/** @brief M0 电机 ADC 实例配置。*/
static const bm_vendor_adc_config_t g_adc_config_m0 = { 0u };
/** @brief M1 电机 ADC 实例配置。*/
static const bm_vendor_adc_config_t g_adc_config_m1 = { 1u };

/** @brief M0 电机 ADC 实例。*/
const bm_hal_adc_t bm_hal_adc_m0 = { &g_adc_api, &g_adc_config_m0 };
/** @brief M1 电机 ADC 实例。*/
const bm_hal_adc_t bm_hal_adc_m1 = { &g_adc_api, &g_adc_config_m1 };
