/**
 * @file bm_vendor_pwm_esp32_idf.c
 * @brief ESP32-WROOM-32E 板级 PWM 裸机实现
 *
 * 该实现只依赖 GPIO LL 与裸机 timer 服务，不引入任何外设驱动层。
 * PWM 输出采用静态状态与确定性轮询驱动，安全态下始终拉低 M_EN 并清零占空比。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            新增双电机 PWM 实现
 * 2026-06-19       1.1            Codex          改为裸机静态 GPIO 实现
 *
 */
#include "bm_vendor_pwm_esp32_idf.h"
#include "bm_hal_instances_esp32wroom32e.h"
#include "bm_types.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hal/gpio_ll.h"
#include "esp_rom_sys.h"

/** @brief 电机实例数。 */
#define BM_VENDOR_PWM_INSTANCE_COUNT  2u
/** @brief 相位数。 */
#define BM_VENDOR_PWM_PHASE_COUNT      3u

typedef struct {
    /** @brief 编号。 */
    uint32_t id;
} bm_vendor_pwm_config_t;

typedef struct {
    /** @brief 当前占空比缓存。 */
    uint16_t duty[BM_VENDOR_PWM_PHASE_COUNT];
    /** @brief 是否已使能输出。 */
    int outputs_enabled;
    /** @brief 是否已完成硬件初始化。 */
    int initialized;
    /** @brief HRT 绑定回调。 */
    bm_hal_hrt_binding_t update_binding;
    /** @brief 软件载波计数。 */
    uint16_t carrier_step;
} bm_vendor_pwm_context_t;

static bm_vendor_pwm_context_t g_pwm_context[BM_VENDOR_PWM_INSTANCE_COUNT];

/**
 * @brief 编码器/采样链路的 PWM 周期触发入口。
 */
extern void bm_vendor_adc_esp32_idf_pwm_tick(uint32_t motor_id);

static const uint32_t g_pwm_phase_gpio[BM_VENDOR_PWM_INSTANCE_COUNT][BM_VENDOR_PWM_PHASE_COUNT] = {
    {
        BM_ESP32WROOM32E_M0_IN1_GPIO,
        BM_ESP32WROOM32E_M0_IN2_GPIO,
        BM_ESP32WROOM32E_M0_IN3_GPIO,
    },
    {
        BM_ESP32WROOM32E_M1_IN1_GPIO,
        BM_ESP32WROOM32E_M1_IN2_GPIO,
        BM_ESP32WROOM32E_M1_IN3_GPIO,
    },
};

static const bm_vendor_pwm_config_t g_pwm_config_m0 = { 0u };
static const bm_vendor_pwm_config_t g_pwm_config_m1 = { 1u };

static bm_vendor_pwm_context_t *bm_vendor_pwm_context_for(const struct bm_hal_pwm *dev) {
    const bm_vendor_pwm_config_t *cfg;

    if (dev == NULL || dev->config == NULL) {
        return NULL;
    }
    cfg = (const bm_vendor_pwm_config_t *)dev->config;
    if (cfg->id >= BM_VENDOR_PWM_INSTANCE_COUNT) {
        return NULL;
    }
    return &g_pwm_context[cfg->id];
}

/**
 * @brief 初始化单个 GPIO 为裸机输出。
 */
static void bm_vendor_pwm_pin_init(uint32_t gpio_num) {
    gpio_ll_func_sel(&GPIO, (uint8_t)gpio_num, 2u);
    gpio_ll_output_enable(&GPIO, gpio_num);
    gpio_ll_input_disable(&GPIO, gpio_num);
    gpio_ll_od_disable(&GPIO, gpio_num);
    gpio_ll_set_drive_capability(&GPIO, gpio_num, GPIO_DRIVE_CAP_3);
    gpio_ll_set_level(&GPIO, gpio_num, 0u);
}

/**
 * @brief 将指定电机的三相输出写入 GPIO。
 */
static void bm_vendor_pwm_commit_levels(const bm_vendor_pwm_context_t *ctx, uint32_t motor_id) {
    uint32_t phase;
    uint32_t compare;

    if (ctx == NULL) {
        return;
    }
    for (phase = 0u; phase < BM_VENDOR_PWM_PHASE_COUNT; ++phase) {
        compare = (uint32_t)ctx->duty[phase];
        if (compare > BOARD_FOC_PWM_MAX) {
            compare = BOARD_FOC_PWM_MAX;
        }
        gpio_ll_set_level(&GPIO,
                          g_pwm_phase_gpio[motor_id][phase],
                          (ctx->outputs_enabled != 0 && ctx->carrier_step < compare) ? 1u : 0u);
    }
}

/**
 * @brief 将总使能脚拉低。
 */
static int bm_vendor_pwm_enable_pin(int enable) {
    gpio_ll_func_sel(&GPIO, (uint8_t)BM_ESP32WROOM32E_M_EN_GPIO, 2u);
    gpio_ll_output_enable(&GPIO, BM_ESP32WROOM32E_M_EN_GPIO);
    gpio_ll_input_disable(&GPIO, BM_ESP32WROOM32E_M_EN_GPIO);
    gpio_ll_od_disable(&GPIO, BM_ESP32WROOM32E_M_EN_GPIO);
    gpio_ll_set_level(&GPIO, BM_ESP32WROOM32E_M_EN_GPIO, enable ? 1u : 0u);
    return BM_OK;
}

/**
 * @brief 硬件初始化。
 */
static int bm_vendor_pwm_hw_init(bm_vendor_pwm_context_t *ctx) {
    uint32_t motor_id;
    uint32_t phase;

    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    if (ctx->initialized != 0) {
        return BM_OK;
    }

    motor_id = (ctx == &g_pwm_context[0]) ? 0u : 1u;
    for (phase = 0u; phase < BM_VENDOR_PWM_PHASE_COUNT; ++phase) {
        bm_vendor_pwm_pin_init(g_pwm_phase_gpio[motor_id][phase]);
    }
    (void)bm_vendor_pwm_enable_pin(0);
    ctx->carrier_step = 0u;
    ctx->initialized = 1;
    return BM_OK;
}

/**
 * @brief 推进一个软件 PWM 子步。
 *
 * 该函数由裸机 timer 服务驱动，用于保持输出状态、执行 ADC 采样以及
 * 调用绑定的 HRT 更新回调。
 */
void bm_vendor_pwm_esp32_idf_timer_tick(void) {
    uint32_t motor_id;
    bm_vendor_pwm_context_t *ctx;

    for (motor_id = 0u; motor_id < BM_VENDOR_PWM_INSTANCE_COUNT; ++motor_id) {
        ctx = &g_pwm_context[motor_id];
        if (ctx->initialized == 0) {
            continue;
        }
        ctx->carrier_step++;
        if (ctx->carrier_step >= BOARD_FOC_PWM_MAX) {
            ctx->carrier_step = 0u;
        }
        bm_vendor_pwm_commit_levels(ctx, motor_id);
        if (ctx->outputs_enabled != 0) {
            bm_vendor_adc_esp32_idf_pwm_tick(motor_id);
        }
        if (ctx->update_binding.callback != NULL) {
            ctx->update_binding.callback(ctx->update_binding.context);
        }
    }
}

static int bm_vendor_pwm_set_duty(const struct bm_hal_pwm *dev, uint32_t phase, uint16_t duty) {
    bm_vendor_pwm_context_t *ctx;
    uint32_t motor_id;

    if (phase >= BM_VENDOR_PWM_PHASE_COUNT || duty > BOARD_FOC_PWM_MAX) {
        return BM_ERR_INVALID;
    }
    ctx = bm_vendor_pwm_context_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_vendor_pwm_hw_init(ctx) != BM_OK) {
        return BM_ERR_IO;
    }
    motor_id = (ctx == &g_pwm_context[0]) ? 0u : 1u;
    ctx->duty[phase] = duty;
    bm_vendor_pwm_commit_levels(ctx, motor_id);
    return BM_OK;
}

static int bm_vendor_pwm_enable_outputs(const struct bm_hal_pwm *dev, int enable) {
    bm_vendor_pwm_context_t *ctx;
    uint32_t motor_id;
    uint32_t phase;

    ctx = bm_vendor_pwm_context_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_vendor_pwm_hw_init(ctx) != BM_OK) {
        return BM_ERR_IO;
    }

    motor_id = (ctx == &g_pwm_context[0]) ? 0u : 1u;
    if (bm_vendor_pwm_enable_pin(enable) != BM_OK) {
        return BM_ERR_IO;
    }

    ctx->outputs_enabled = enable ? 1 : 0;
    if (ctx->outputs_enabled == 0) {
        for (phase = 0u; phase < BM_VENDOR_PWM_PHASE_COUNT; ++phase) {
            ctx->duty[phase] = 0u;
        }
    }
    bm_vendor_pwm_commit_levels(ctx, motor_id);
    return BM_OK;
}

static int bm_vendor_pwm_request_safe_state(const struct bm_hal_pwm *dev) {
    bm_vendor_pwm_context_t *ctx;
    uint32_t motor_id;
    uint32_t phase;

    ctx = bm_vendor_pwm_context_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_vendor_pwm_hw_init(ctx) != BM_OK) {
        return BM_ERR_IO;
    }

    motor_id = (ctx == &g_pwm_context[0]) ? 0u : 1u;
    ctx->outputs_enabled = 0;
    ctx->carrier_step = 0u;
    for (phase = 0u; phase < BM_VENDOR_PWM_PHASE_COUNT; ++phase) {
        ctx->duty[phase] = 0u;
    }
    bm_vendor_pwm_commit_levels(ctx, motor_id);
    if (bm_vendor_pwm_enable_pin(0) != BM_OK) {
        return BM_ERR_IO;
    }
    return BM_OK;
}

static int bm_vendor_pwm_bind_update(const struct bm_hal_pwm *dev,
                                     const bm_hal_hrt_binding_t *binding) {
    bm_vendor_pwm_context_t *ctx;

    ctx = bm_vendor_pwm_context_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    if (binding == NULL) {
        memset(&ctx->update_binding, 0, sizeof(ctx->update_binding));
        return BM_OK;
    }
    ctx->update_binding = *binding;
    return BM_OK;
}

static const struct bm_pwm_driver_api g_pwm_api = {
    bm_vendor_pwm_set_duty,
    bm_vendor_pwm_enable_outputs,
    bm_vendor_pwm_request_safe_state,
    bm_vendor_pwm_bind_update,
};

const bm_hal_pwm_t bm_hal_pwm_m0 = { &g_pwm_api, &g_pwm_config_m0 };
const bm_hal_pwm_t bm_hal_pwm_m1 = { &g_pwm_api, &g_pwm_config_m1 };
