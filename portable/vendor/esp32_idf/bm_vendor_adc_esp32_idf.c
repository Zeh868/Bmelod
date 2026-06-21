/**
 * @file bm_vendor_adc_esp32_idf.c
 * @brief ESP32-WROOM-32E ADC1 采样实现（Phase 2：ISR 内软触发）
 *
 * 本实现仅使用 ESP-IDF 5.2.3 的 ADC LL / SoC 寄存器头文件，不依赖驱动层、
 * FreeRTOS 任务或队列。采样由 MCPWM TEZ ISR 周期触发（oneshot 软触发模式）。
 *
 * @note Phase 2 修正（相较 Phase 1）：
 *       ① 删除 `adc_ll_enable_bus_clock`——IDF 5.2.3 esp32 不存在此函数
 *          （5.2.3 esp32 上 ADC 时钟随 APB 始终开启，无需显式开关）。
 *       ② 删除 `adc_ll_reset_register`——IDF 5.2.3 esp32 不存在此函数。
 *       ③ `ADC_ATTEN_DB_11` → `ADC_ATTEN_DB_12`（5.2.3 中前者已废弃）。
 *       ④ 采样入口改为 `bm_vendor_adc_esp32_idf_isr_sample`，由 MCPWM ISR
 *          调用；`read_injected` 返回缓存值。
 *
 * @note ESP32 经典 ADC 在 ISR 内 oneshot 转换有 µs 级延迟（无 ETM 硬触发路径），
 *       高频电流环（20 kHz ISR）的 ADC 采样延迟需待硬件验证。
 *
 * @author zeh (china_qzh@163.com)
 * @version 2.0
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            新增双电机 ADC 实例
 * 2026-06-19       1.1            zeh            改为 ADC LL 裸机实现
 * 2026-06-19       2.0            zeh            Phase 2：改为 MCPWM ISR 触发采样
 *
 */
#include "bm_vendor_adc_esp32_idf.h"
#include "bm_vendor_pwm_esp32_idf.h"
#include "bm_vendor_esp32_idf_compat.h"
#include "bm_hal_instances_esp32wroom32e.h"
#include "bm_types.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(BM_ESP32_BAREMETAL)
#include "hal/adc_ll.h"
#endif

/** @brief 电机实例数量。 */
#define BM_VENDOR_ADC_INSTANCE_COUNT  2u
/** @brief 单个实例的采样 rank 数（ia / ib 两路）。 */
#define BM_VENDOR_ADC_RANK_COUNT      2u
/** @brief ISR 内 oneshot 转换的最大等待循环次数。 */
#define BM_VENDOR_ADC_POLL_LIMIT      2048u

typedef struct {
    /** @brief 电机编号（0/1）。 */
    uint32_t id;
} bm_vendor_adc_config_t;

typedef struct {
    /** @brief 是否已完成硬件初始化。 */
    int initialized;
    /** @brief ISR 内采样的原始 ADC 缓存值。 */
    uint16_t cached[BM_VENDOR_ADC_RANK_COUNT];
    /** @brief HRT 完成回调绑定（透传到 MCPWM ISR）。 */
    bm_hal_hrt_binding_t complete_binding;
} bm_vendor_adc_context_t;

/** @brief M0 / M1 独立上下文。 */
static bm_vendor_adc_context_t g_adc_context[BM_VENDOR_ADC_INSTANCE_COUNT];

/** @brief M0 ADC1 通道（ia=CH3/GPIO39，ib=CH0/GPIO36）。 */
static const int g_adc_channels_m0[BM_VENDOR_ADC_RANK_COUNT] = {
    3,
    0,
};

/** @brief M1 ADC1 通道（ia=CH7/GPIO35，ib=CH6/GPIO34）。 */
static const int g_adc_channels_m1[BM_VENDOR_ADC_RANK_COUNT] = {
    7,
    6,
};

/**
 * @brief ADC 组件中心点（与 motor_foc_sensored 组件硬编码一致）。
 *
 * 组件 adc_to_current(scale,raw) = ((int32)raw − 32768) / scale。本驱动在
 * read_injected 返回前把 12bit raw 平移到该中心：raw' = (raw − zero_offset)
 * + 32768，使组件 (raw'−32768)/scale = (raw−zero_offset)/scale 等价于
 * 项目 A1 标定 I = (raw − zero_offset) × (1/scale)。
 */
#define BM_VENDOR_ADC_CENTER  32768

/**
 * @brief 每电机的电流零偏（raw，板级 config，per-motor）。
 *
 * 来自项目 A1 标定（INA240 零点偏置实测）：M0/M1 均 1853。本驱动据此在返回前
 * 做中心化，使组件可直接套用 current_adc_scale=1/0.00160≈625 而无需改组件。
 * 切到本 vendor 路径后该零偏需上板按 A1 流程重核一次（vendor ISR oneshot 与
 * hover_adc 16 样均值可能有微差）。
 */
static const int32_t g_adc_zero_offset[BM_VENDOR_ADC_INSTANCE_COUNT] = {
    1853,  /* M0：A1 实测零偏 raw */
    1853,  /* M1：A1 暂复用 M0 零偏，上板按 A1 重核 */
};

/**
 * @brief 从设备实例提取板级上下文。
 * @param dev HAL 设备实例。
 * @return 板级上下文；无效时返回 NULL。
 */
static bm_vendor_adc_context_t *bm_vendor_adc_context_for(const struct bm_hal_adc *dev)
{
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
 * @brief 初始化单个 ADC1 实例（RTC 控制器，oneshot 模式）。
 *
 * IDF 5.2.3 esp32 注意事项：
 *   - 不调用 adc_ll_enable_bus_clock（5.2.3 esp32 无此函数，APB 时钟始终开启）
 *   - 不调用 adc_ll_reset_register（5.2.3 esp32 无此函数）
 *   - 衰减使用 ADC_ATTEN_DB_12（替代已废弃的 ADC_ATTEN_DB_11）
 *
 * @param ctx 板级上下文。
 * @return BM_OK 成功；否则为平台错误码。
 */
static int bm_vendor_adc_hw_init(bm_vendor_adc_context_t *ctx)
{
    const int *channels;
    uint32_t   rank;

    if (ctx == NULL || ctx->initialized) {
        return BM_OK;
    }

    /* 选择 RTC 控制器（oneshot 软触发路径） */
    adc_ll_set_controller(ADC_UNIT_1, ADC_LL_CTRL_RTC);
    adc_oneshot_ll_set_output_bits(ADC_UNIT_1, ADC_BITWIDTH_12);
    adc_oneshot_ll_enable(ADC_UNIT_1);

    channels = (ctx == &g_adc_context[0]) ? g_adc_channels_m0 : g_adc_channels_m1;
    for (rank = 0u; rank < BM_VENDOR_ADC_RANK_COUNT; ++rank) {
        /* Phase 2：使用 ADC_ATTEN_DB_12（5.2.3 中 DB_11 已废弃） */
        adc_oneshot_ll_set_atten(ADC_UNIT_1,
                                 (adc_channel_t)channels[rank],
                                 ADC_ATTEN_DB_12);
    }

    ctx->initialized = 1;
    return BM_OK;
}

/**
 * @brief 在 ISR 内对单个电机执行 oneshot ADC 采样并更新缓存。
 *
 * 由 MCPWM TEZ ISR 调用。每次调用对两路通道（ia/ib）依次转换。
 *
 * @note ESP32 经典 ADC oneshot 转换在 ISR 内有 µs 级轮询延迟，
 *       20 kHz 电流环性能为待硬件验证项。
 *
 * @param ctx 板级上下文。
 * @return BM_OK 成功；否则为平台错误码。
 */
static int bm_vendor_adc_isr_sample(bm_vendor_adc_context_t *ctx)
{
    const int *channels;
    uint32_t   rank;
    uint32_t   wait;

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
#endif /* BM_ESP32_BAREMETAL */

/**
 * @brief MCPWM TEZ ISR 内的 ADC 采样入口（由 PWM 驱动 ISR 调用）。
 *
 * 在 ISR 上下文中触发 oneshot 采样，更新缓存；完成回调通过
 * MCPWM ISR 中的 adc_complete_binding 分发（不在本函数内调用）。
 *
 * @param motor_id 电机编号（0/1）。
 */
void bm_vendor_adc_esp32_idf_isr_sample(uint32_t motor_id)
{
    bm_vendor_adc_context_t *ctx;

    if (motor_id >= BM_VENDOR_ADC_INSTANCE_COUNT) {
        return;
    }
    ctx = &g_adc_context[motor_id];

#if defined(BM_ESP32_BAREMETAL)
    (void)bm_vendor_adc_isr_sample(ctx);
#endif
    (void)ctx;
}

/**
 * @brief 读取缓存的注入通道值并中心化（ISR 后由控制环读取）。
 *
 * 在返回前把 12bit raw 平移到组件中心点：raw' = clamp((raw − zero_offset)
 * + 32768, 0..65535)。zero_offset 取本电机板级零偏（g_adc_zero_offset）。
 * 这样上层 motor_foc_sensored 组件 (raw'−32768)/scale 即等价于 A1 标定
 * (raw − zero_offset) × (1/scale)，组件无需改动、可跨板复用。
 *
 * @param dev   HAL 设备实例。
 * @param rank  采样序号（0=ia，1=ib）。
 * @param value 输出值（已中心化的 16bit 等效 raw）。
 * @return BM_OK 成功；否则为错误码。
 */
static int bm_vendor_adc_read_injected(const struct bm_hal_adc *dev,
                                       uint32_t rank, uint16_t *value)
{
    const bm_vendor_adc_config_t *cfg;
    bm_vendor_adc_context_t      *ctx;
    int32_t                       centered;

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
    cfg = (const bm_vendor_adc_config_t *)dev->config;
    /* 板级零偏中心化：(raw − zero_offset) + 32768，再饱和到 uint16 范围。 */
    centered = (int32_t)ctx->cached[rank]
               - g_adc_zero_offset[cfg->id]
               + BM_VENDOR_ADC_CENTER;
    if (centered < 0) {
        centered = 0;
    } else if (centered > 65535) {
        centered = 65535;
    }
    *value = (uint16_t)centered;
    return BM_OK;
}

/**
 * @brief 绑定采样完成回调（透传到对应电机的 MCPWM ISR）。
 * @param dev     HAL 设备实例。
 * @param binding HRT 绑定；NULL 表示清除。
 * @return BM_OK 成功；否则为错误码。
 */
static int bm_vendor_adc_bind_complete(const struct bm_hal_adc *dev,
                                       const bm_hal_hrt_binding_t *binding)
{
    const bm_vendor_adc_config_t *cfg;
    bm_vendor_adc_context_t      *ctx;

    ctx = bm_vendor_adc_context_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    if (binding == NULL) {
        memset(&ctx->complete_binding, 0, sizeof(ctx->complete_binding));
    } else {
        ctx->complete_binding = *binding;
    }

    /* 同步到 MCPWM ISR 上下文中的 adc_complete_binding */
    cfg = (const bm_vendor_adc_config_t *)dev->config;
    bm_vendor_pwm_esp32_idf_bind_adc_complete(cfg->id, binding);
    return BM_OK;
}

/** @brief ADC 驱动 API 表。 */
static const struct bm_adc_driver_api g_adc_api = {
    bm_vendor_adc_read_injected,
    bm_vendor_adc_bind_complete,
};

/** @brief M0 电机 ADC 实例配置。 */
static const bm_vendor_adc_config_t g_adc_config_m0 = { 0u };
/** @brief M1 电机 ADC 实例配置。 */
static const bm_vendor_adc_config_t g_adc_config_m1 = { 1u };

/** @brief M0 电机 ADC 实例。 */
const bm_hal_adc_t bm_hal_adc_m0 = { &g_adc_api, &g_adc_config_m0 };
/** @brief M1 电机 ADC 实例。 */
const bm_hal_adc_t bm_hal_adc_m1 = { &g_adc_api, &g_adc_config_m1 };
