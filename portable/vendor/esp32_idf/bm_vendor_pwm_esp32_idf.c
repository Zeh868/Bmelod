/**
 * @file bm_vendor_pwm_esp32_idf.c
 * @brief ESP32-WROOM-32E 硬件 MCPWM 驱动实现（Phase 2）
 *
 * 本实现基于 ESP-IDF 5.2.3 MCPWM LL API，将三相单端 PWM 输出配置为：
 *   - 中心对齐（up-down 计数），20 kHz，比较量程 BOARD_FOC_PWM_MAX(1000)
 *   - M0 → MCPWM unit0（operator 0/1/2，各取 generator A 单端输出）
 *   - M1 → MCPWM unit1（operator 0/1/2，各取 generator A 单端输出）
 *   - GPIO 通过 GPIO matrix（IOMUX func=2）映射到 MCPWM 信号
 *   - PWM TEZ（计数到零）触发控制 ISR，在 ISR 内软触发 ADC 采样
 *   - 安全态：generator 强制持续低电平 + M_EN GPIO 拉低
 *
 * @note 死区由外部栅驱处理，本驱动不生成互补对。
 * @note ESP32 经典 ADC 在 ISR 内 oneshot 转换有 µs 级延迟，高频电流环
 *       性能为待硬件验证项。
 *
 * @author zeh (china_qzh@163.com)
 * @version 2.0
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            新增双电机 PWM 实现
 * 2026-06-19       1.1            Codex          改为裸机静态 GPIO 实现
 * 2026-06-19       2.0            zeh            重写为硬件 MCPWM + ISR 驱动（Phase 2）
 *
 */
#include "bm_vendor_pwm_esp32_idf.h"
#include "bm_vendor_esp32_idf_compat.h"
#include "bm_hal_instances_esp32wroom32e.h"
#include "bm_types.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hal/gpio_ll.h"
#include "hal/mcpwm_ll.h"
#include "soc/interrupts.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"

/** @brief 电机实例数。 */
#define BM_VENDOR_PWM_INSTANCE_COUNT  2u
/** @brief 每电机使用的相位（operator）数量。 */
#define BM_VENDOR_PWM_PHASE_COUNT     3u
/** @brief 每个 operator 使用的 timer 编号（M0 用 timer0，M1 亦用 timer0）。 */
#define BM_VENDOR_PWM_TIMER_ID        0
/** @brief 每个 operator 使用的 compare 编号（comparator A）。 */
#define BM_VENDOR_PWM_CMP_ID          0
/** @brief 每个 operator 使用的 generator 编号（generator A / PWMxA）。 */
#define BM_VENDOR_PWM_GEN_ID          0

/**
 * @brief MCPWM 时钟源频率。
 *
 * ESP32 MCPWM group 时钟来自 160 MHz APB / group_prescale。
 * group_prescale = 1（寄存器写 0）时 group_clk = 160 MHz。
 * timer_prescale 按如下公式：timer_clk = 160M / timer_prescale。
 * up-down 模式下：PWM_freq = timer_clk / (2 * peak)。
 * 目标：20 kHz，peak = BOARD_FOC_PWM_MAX = 1000。
 * => timer_prescale = 160M / (2 * 1000 * 20000) = 4。
 */
#define BM_VENDOR_PWM_GROUP_PRESCALE  1
#define BM_VENDOR_PWM_TIMER_PRESCALE  4u
/** @brief up-down 中心对齐 peak 值（与 BOARD_FOC_PWM_MAX 对齐）。 */
#define BM_VENDOR_PWM_PEAK            BOARD_FOC_PWM_MAX

typedef struct {
    /** @brief 编号（0=M0, 1=M1）。 */
    uint32_t id;
} bm_vendor_pwm_config_t;

typedef struct {
    /** @brief 当前占空比缓存（写入 MCPWM 比较器）。 */
    uint16_t duty[BM_VENDOR_PWM_PHASE_COUNT];
    /** @brief 是否已使能输出。 */
    int outputs_enabled;
    /** @brief 是否已完成硬件初始化。 */
    int initialized;
    /** @brief HRT 更新回调绑定。 */
    bm_hal_hrt_binding_t update_binding;
    /** @brief HRT ADC 完成回调绑定（由 ADC 模块设置）。 */
    bm_hal_hrt_binding_t adc_complete_binding;
    /** @brief ISR handle。 */
    intr_handle_t isr_handle;
} bm_vendor_pwm_context_t;

/** @brief 两个电机的 PWM 上下文。 */
static bm_vendor_pwm_context_t g_pwm_context[BM_VENDOR_PWM_INSTANCE_COUNT];

/** @brief M0 / M1 静态配置。 */
static const bm_vendor_pwm_config_t g_pwm_config_m0 = { 0u };
static const bm_vendor_pwm_config_t g_pwm_config_m1 = { 1u };

/**
 * @brief 每电机、每相的 GPIO 映射（operator 0/1/2 → IN1/IN2/IN3）。
 */
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

/**
 * @brief MCPWM unit0/1 的 IOMUX 信号编号（PWMxA，operator 0/1/2）。
 *
 * 来自 ESP32 TRM IOMUX 章节：
 *   MCPWM0 operator0 PWMA = GPIO_FUNC_IN/OUT signal 86
 *   MCPWM0 operator1 PWMA = GPIO_FUNC_IN/OUT signal 88
 *   MCPWM0 operator2 PWMA = GPIO_FUNC_IN/OUT signal 90
 *   MCPWM1 operator0 PWMA = GPIO_FUNC_IN/OUT signal 94 (PWM1 0A)
 *   MCPWM1 operator1 PWMA = GPIO_FUNC_IN/OUT signal 96
 *   MCPWM1 operator2 PWMA = GPIO_FUNC_IN/OUT signal 98
 *
 * @note 待硬件验证：信号编号通过 gpio_matrix_out 路由，实际出波需上板确认。
 */
static const int g_mcpwm_signal[BM_VENDOR_PWM_INSTANCE_COUNT][BM_VENDOR_PWM_PHASE_COUNT] = {
    { 86, 88, 90 },  /* MCPWM0 OP0A, OP1A, OP2A */
    { 94, 96, 98 },  /* MCPWM1 OP0A, OP1A, OP2A */
};

/* ---------- 前向声明 ---------- */

/**
 * @brief ADC 在 ISR 内的采样入口（由 MCPWM ISR 调用）。
 * @param motor_id 电机编号。
 */
extern void bm_vendor_adc_esp32_idf_isr_sample(uint32_t motor_id);

/* ---------- 辅助函数 ---------- */

/**
 * @brief 从设备实例提取板级上下文。
 * @param dev HAL 设备实例。
 * @return 板级上下文；无效时返回 NULL。
 */
static bm_vendor_pwm_context_t *bm_vendor_pwm_context_for(const struct bm_hal_pwm *dev)
{
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
 * @brief 配置 M_EN GPIO 输出电平。
 * @param enable 非零为拉高（使能），0 为拉低（安全态）。
 * @return BM_OK。
 */
static int bm_vendor_pwm_set_en_pin(int enable)
{
    gpio_ll_func_sel(&GPIO, (uint8_t)BM_ESP32WROOM32E_M_EN_GPIO, 2u);
    gpio_ll_output_enable(&GPIO, BM_ESP32WROOM32E_M_EN_GPIO);
    gpio_ll_input_disable(&GPIO, BM_ESP32WROOM32E_M_EN_GPIO);
    gpio_ll_od_disable(&GPIO, BM_ESP32WROOM32E_M_EN_GPIO);
    gpio_ll_set_level(&GPIO, BM_ESP32WROOM32E_M_EN_GPIO, enable ? 1u : 0u);
    return BM_OK;
}

/**
 * @brief 通过 GPIO matrix 将 GPIO 路由到 MCPWM 信号输出。
 * @param gpio_num GPIO 编号。
 * @param signal   MCPWM 输出信号编号（来自 g_mcpwm_signal）。
 */
static void bm_vendor_pwm_route_gpio(uint32_t gpio_num, int signal)
{
    /* 配置 GPIO 为输出，使用 IOMUX 功能 2（GPIO matrix）*/
    gpio_ll_func_sel(&GPIO, (uint8_t)gpio_num, 2u);
    gpio_ll_output_enable(&GPIO, gpio_num);
    gpio_ll_input_disable(&GPIO, gpio_num);
    gpio_ll_od_disable(&GPIO, gpio_num);
    gpio_ll_set_drive_capability(&GPIO, gpio_num, GPIO_DRIVE_CAP_3);
    /* 通过 GPIO matrix 将 MCPWM 信号连接到该 GPIO */
    GPIO.func_out_sel_cfg[gpio_num].func_sel = (uint32_t)signal;
    GPIO.func_out_sel_cfg[gpio_num].inv_sel  = 0u;
    GPIO.func_out_sel_cfg[gpio_num].oen_sel  = 0u;
    GPIO.func_out_sel_cfg[gpio_num].oen_inv_sel = 0u;
}

/**
 * @brief 对指定 MCPWM unit 初始化 timer（timer0）。
 * @param hw   MCPWM 硬件实例。
 */
static void bm_vendor_pwm_timer_init(mcpwm_dev_t *hw)
{
    /* group prescale = 1 (reg = 0) → group_clk = 160 MHz */
    mcpwm_ll_group_set_clock_prescale(hw, BM_VENDOR_PWM_GROUP_PRESCALE);
    mcpwm_ll_group_enable_shadow_mode(hw);

    /* timer0 prescale = 4 → timer_clk = 40 MHz */
    mcpwm_ll_timer_set_clock_prescale(hw, BM_VENDOR_PWM_TIMER_ID,
                                      BM_VENDOR_PWM_TIMER_PRESCALE);
    /* up-down 中心对齐，peak = 1000 */
    mcpwm_ll_timer_set_count_mode(hw, BM_VENDOR_PWM_TIMER_ID,
                                  MCPWM_TIMER_COUNT_MODE_UP_DOWN);
    mcpwm_ll_timer_set_peak(hw, BM_VENDOR_PWM_TIMER_ID,
                            (uint32_t)BM_VENDOR_PWM_PEAK, true);
    mcpwm_ll_timer_update_period_at_once(hw, BM_VENDOR_PWM_TIMER_ID);
}

/**
 * @brief 对单个 operator 初始化：绑定 timer，配置比较器更新方式，
 *        设置 generator A 动作（上升沿高→下降沿比较低，中心对齐单端）。
 * @param hw          MCPWM 硬件实例。
 * @param operator_id operator 编号（0/1/2）。
 */
static void bm_vendor_pwm_operator_init(mcpwm_dev_t *hw, int operator_id)
{
    /* operator 连接到 timer0 */
    mcpwm_ll_operator_connect_timer(hw, operator_id, BM_VENDOR_PWM_TIMER_ID);

    /* 比较值在 TEZ（计数到零）时更新 */
    mcpwm_ll_operator_enable_update_compare_on_tez(hw, operator_id, BM_VENDOR_PWM_CMP_ID, true);
    mcpwm_ll_operator_set_compare_value(hw, operator_id, BM_VENDOR_PWM_CMP_ID, 0u);

    /* actions 立即生效 */
    mcpwm_ll_operator_update_action_at_once(hw, operator_id);

    /* generator A：上计数到比较值时置高，下计数到比较值时置低 */
    mcpwm_ll_generator_reset_actions(hw, operator_id, BM_VENDOR_PWM_GEN_ID);
    mcpwm_ll_generator_set_action_on_compare_event(
        hw, operator_id, BM_VENDOR_PWM_GEN_ID,
        MCPWM_TIMER_DIRECTION_UP, BM_VENDOR_PWM_CMP_ID,
        MCPWM_GEN_ACTION_HIGH);
    mcpwm_ll_generator_set_action_on_compare_event(
        hw, operator_id, BM_VENDOR_PWM_GEN_ID,
        MCPWM_TIMER_DIRECTION_DOWN, BM_VENDOR_PWM_CMP_ID,
        MCPWM_GEN_ACTION_LOW);

    /* 初始强制低电平（安全态）*/
    mcpwm_ll_gen_set_continue_force_level(hw, operator_id, BM_VENDOR_PWM_GEN_ID, 0);
}

/**
 * @brief MCPWM TEZ 中断服务函数（motor_id 通过 arg 传入）。
 *
 * 在 TEZ 事件（计数到零）时执行：
 *   1. 清除中断标志
 *   2. 软触发 ADC 采样
 *   3. 调用 ADC 完成 HRT 回调
 *   4. 调用 PWM 更新 HRT 回调
 *
 * @param arg 指向 bm_vendor_pwm_context_t 的指针。
 */
static void IRAM_ATTR bm_vendor_pwm_isr(void *arg)
{
    bm_vendor_pwm_context_t *ctx;
    mcpwm_dev_t             *hw;
    uint32_t                 motor_id;
    uint32_t                 status;

    ctx = (bm_vendor_pwm_context_t *)arg;
    if (ctx == NULL) {
        return;
    }
    motor_id = (ctx == &g_pwm_context[0]) ? 0u : 1u;
    hw = MCPWM_LL_GET_HW((int)motor_id);

    /* 读取并清除 TEZ 中断（timer0 empty event = bit 3） */
    status = mcpwm_ll_intr_get_status(hw);
    mcpwm_ll_intr_clear_status(hw, status);

    if ((status & MCPWM_LL_EVENT_TIMER_EMPTY(BM_VENDOR_PWM_TIMER_ID)) == 0u) {
        return;
    }

    /* ADC 在 ISR 内软触发采样（ESP32 经典无 ETM 硬触发路径） */
    bm_vendor_adc_esp32_idf_isr_sample(motor_id);

    /* ADC 完成回调（由 ADC 模块注册） */
    if (ctx->adc_complete_binding.callback != NULL) {
        ctx->adc_complete_binding.callback(ctx->adc_complete_binding.context);
    }

    /* PWM 更新回调 */
    if (ctx->update_binding.callback != NULL) {
        ctx->update_binding.callback(ctx->update_binding.context);
    }
}

/**
 * @brief 初始化单个电机的 MCPWM 硬件。
 * @param ctx      板级上下文。
 * @return BM_OK 成功；否则为平台错误码。
 */
static int bm_vendor_pwm_hw_init(bm_vendor_pwm_context_t *ctx)
{
    uint32_t     motor_id;
    uint32_t     phase;
    mcpwm_dev_t *hw;
    int          intr_src;
    int          ret;

    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    if (ctx->initialized != 0) {
        return BM_OK;
    }

    motor_id = (ctx == &g_pwm_context[0]) ? 0u : 1u;
    hw       = MCPWM_LL_GET_HW((int)motor_id);

    /*
     * 使能 MCPWM 总线时钟。
     * BM_PERIPH_RCC_ATOMIC_BEGIN/END（bm_vendor_esp32_idf_compat.h）：
     *   - 真实 IDF：PERIPH_RCC_ATOMIC(){...} 临界块
     *   - compilecheck：带守卫变量的普通块（满足 mcpwm_ll.h 宏的编译期要求）
     */
    BM_PERIPH_RCC_ATOMIC_BEGIN
        mcpwm_ll_enable_bus_clock((int)motor_id, true);
        mcpwm_ll_reset_register((int)motor_id);
    BM_PERIPH_RCC_ATOMIC_END

    /* 初始化 timer */
    bm_vendor_pwm_timer_init(hw);

    /* 初始化 3 个 operator */
    for (phase = 0u; phase < BM_VENDOR_PWM_PHASE_COUNT; ++phase) {
        bm_vendor_pwm_operator_init(hw, (int)phase);
    }

    /* 通过 GPIO matrix 连接 MCPWM 输出到相位 GPIO */
    for (phase = 0u; phase < BM_VENDOR_PWM_PHASE_COUNT; ++phase) {
        bm_vendor_pwm_route_gpio(g_pwm_phase_gpio[motor_id][phase],
                                 g_mcpwm_signal[motor_id][phase]);
    }

    /* M_EN 初始低电平（安全态） */
    (void)bm_vendor_pwm_set_en_pin(0);

    /* 使能 timer0 TEZ 中断 */
    mcpwm_ll_intr_enable(hw,
                         MCPWM_LL_EVENT_TIMER_EMPTY(BM_VENDOR_PWM_TIMER_ID),
                         true);

    /* 注册 ISR */
    intr_src = (motor_id == 0u) ? ETS_PWM0_INTR_SOURCE : ETS_PWM1_INTR_SOURCE;
    ret = esp_intr_alloc(intr_src,
                         ESP_INTR_FLAG_LEVEL3 | ESP_INTR_FLAG_IRAM,
                         bm_vendor_pwm_isr,
                         ctx,
                         &ctx->isr_handle);
    if (ret != 0) {
        return BM_ERR_IO;
    }

    /* 启动 timer（连续运行，不自停） */
    mcpwm_ll_timer_set_start_stop_command(hw, BM_VENDOR_PWM_TIMER_ID,
                                          MCPWM_TIMER_START_NO_STOP);

    ctx->initialized = 1;
    return BM_OK;
}

/**
 * @brief 将新占空比值写入 MCPWM 比较器。
 * @param motor_id 电机编号（0 或 1）。
 * @param phase    相位索引（0/1/2）。
 * @param duty     比较值（0..BOARD_FOC_PWM_MAX）。
 */
static void bm_vendor_pwm_write_cmp(uint32_t motor_id, uint32_t phase, uint16_t duty)
{
    mcpwm_dev_t *hw;
    uint32_t     cmp_val;

    hw = MCPWM_LL_GET_HW((int)motor_id);
    cmp_val = (uint32_t)duty;
    if (cmp_val > (uint32_t)BM_VENDOR_PWM_PEAK) {
        cmp_val = (uint32_t)BM_VENDOR_PWM_PEAK;
    }
    mcpwm_ll_operator_set_compare_value(hw, (int)phase, BM_VENDOR_PWM_CMP_ID, cmp_val);
}

/**
 * @brief 强制所有 generator 为持续低电平（安全态 / 禁用输出）。
 * @param motor_id 电机编号。
 */
static void bm_vendor_pwm_force_all_low(uint32_t motor_id)
{
    mcpwm_dev_t *hw;
    uint32_t     phase;

    hw = MCPWM_LL_GET_HW((int)motor_id);
    for (phase = 0u; phase < BM_VENDOR_PWM_PHASE_COUNT; ++phase) {
        mcpwm_ll_gen_set_continue_force_level(hw, (int)phase, BM_VENDOR_PWM_GEN_ID, 0);
    }
}

/**
 * @brief 解除 generator 强制状态（允许 PWM 正常输出）。
 * @param motor_id 电机编号。
 */
static void bm_vendor_pwm_release_force(uint32_t motor_id)
{
    mcpwm_dev_t *hw;
    uint32_t     phase;

    hw = MCPWM_LL_GET_HW((int)motor_id);
    for (phase = 0u; phase < BM_VENDOR_PWM_PHASE_COUNT; ++phase) {
        mcpwm_ll_gen_disable_continue_force_action(hw, (int)phase, BM_VENDOR_PWM_GEN_ID);
    }
}

/* ---------- HAL API 实现 ---------- */

/**
 * @brief 设置指定相位的 PWM 占空比。
 * @param dev   HAL 设备实例。
 * @param phase 相位索引（0/1/2）。
 * @param duty  比较值（0..BOARD_FOC_PWM_MAX）。
 * @return BM_OK 成功；否则为平台错误码。
 */
static int bm_vendor_pwm_set_duty(const struct bm_hal_pwm *dev,
                                  uint32_t phase, uint16_t duty)
{
    bm_vendor_pwm_context_t *ctx;
    uint32_t                 motor_id;

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
    bm_vendor_pwm_write_cmp(motor_id, phase, duty);
    return BM_OK;
}

/**
 * @brief 使能或禁用 PWM 输出。
 * @param dev    HAL 设备实例。
 * @param enable 非零使能，0 禁用（强制低电平）。
 * @return BM_OK 成功；否则为平台错误码。
 */
static int bm_vendor_pwm_enable_outputs(const struct bm_hal_pwm *dev, int enable)
{
    bm_vendor_pwm_context_t *ctx;
    uint32_t                 motor_id;
    uint32_t                 phase;

    ctx = bm_vendor_pwm_context_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_vendor_pwm_hw_init(ctx) != BM_OK) {
        return BM_ERR_IO;
    }
    motor_id = (ctx == &g_pwm_context[0]) ? 0u : 1u;
    ctx->outputs_enabled = enable ? 1 : 0;

    if (ctx->outputs_enabled != 0) {
        /* 解除强制，允许 MCPWM 正常输出 */
        bm_vendor_pwm_release_force(motor_id);
        (void)bm_vendor_pwm_set_en_pin(1);
    } else {
        /* 禁用：清零占空比 + 强制低电平 + EN 拉低 */
        for (phase = 0u; phase < BM_VENDOR_PWM_PHASE_COUNT; ++phase) {
            ctx->duty[phase] = 0u;
            bm_vendor_pwm_write_cmp(motor_id, phase, 0u);
        }
        bm_vendor_pwm_force_all_low(motor_id);
        (void)bm_vendor_pwm_set_en_pin(0);
    }
    return BM_OK;
}

/**
 * @brief 请求 PWM 进入硬件安全态（立即关断所有输出）。
 * @param dev HAL 设备实例。
 * @return BM_OK 成功；否则为平台错误码。
 */
static int bm_vendor_pwm_request_safe_state(const struct bm_hal_pwm *dev)
{
    bm_vendor_pwm_context_t *ctx;
    uint32_t                 motor_id;
    uint32_t                 phase;

    ctx = bm_vendor_pwm_context_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_vendor_pwm_hw_init(ctx) != BM_OK) {
        return BM_ERR_IO;
    }
    motor_id = (ctx == &g_pwm_context[0]) ? 0u : 1u;
    ctx->outputs_enabled = 0;
    for (phase = 0u; phase < BM_VENDOR_PWM_PHASE_COUNT; ++phase) {
        ctx->duty[phase] = 0u;
        bm_vendor_pwm_write_cmp(motor_id, phase, 0u);
    }
    /* generator 强制持续低 + M_EN 拉低（安全态） */
    bm_vendor_pwm_force_all_low(motor_id);
    if (bm_vendor_pwm_set_en_pin(0) != BM_OK) {
        return BM_ERR_IO;
    }
    return BM_OK;
}

/**
 * @brief 绑定 PWM 更新事件到 HRT 回调。
 * @param dev     HAL 设备实例。
 * @param binding HRT 绑定；NULL 表示清除。
 * @return BM_OK 成功；否则为平台错误码。
 */
static int bm_vendor_pwm_bind_update(const struct bm_hal_pwm *dev,
                                     const bm_hal_hrt_binding_t *binding)
{
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

/**
 * @brief 绑定 ADC 完成事件到 MCPWM ISR（由 ADC 模块调用）。
 *
 * ADC 驱动注册完成回调时通过此函数将回调存入 PWM 上下文，
 * MCPWM TEZ ISR 将在 ADC 采样后触发该回调。
 *
 * @param motor_id 电机编号（0/1）。
 * @param binding  HRT 绑定；NULL 表示清除。
 */
void bm_vendor_pwm_esp32_idf_bind_adc_complete(uint32_t motor_id,
                                                const bm_hal_hrt_binding_t *binding)
{
    bm_vendor_pwm_context_t *ctx;

    if (motor_id >= BM_VENDOR_PWM_INSTANCE_COUNT) {
        return;
    }
    ctx = &g_pwm_context[motor_id];
    if (binding == NULL) {
        memset(&ctx->adc_complete_binding, 0, sizeof(ctx->adc_complete_binding));
        return;
    }
    ctx->adc_complete_binding = *binding;
}

/** @brief PWM HAL 驱动 API 表。 */
static const struct bm_pwm_driver_api g_pwm_api = {
    bm_vendor_pwm_set_duty,
    bm_vendor_pwm_enable_outputs,
    bm_vendor_pwm_request_safe_state,
    bm_vendor_pwm_bind_update,
};

/** @brief M0 电机 PWM 实例。 */
const bm_hal_pwm_t bm_hal_pwm_m0 = { &g_pwm_api, &g_pwm_config_m0 };
/** @brief M1 电机 PWM 实例。 */
const bm_hal_pwm_t bm_hal_pwm_m1 = { &g_pwm_api, &g_pwm_config_m1 };
