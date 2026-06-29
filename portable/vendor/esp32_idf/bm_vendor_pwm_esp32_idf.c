/* SPDX-License-Identifier: GPL-3.0-or-later */
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
 * @version 3.2
 * @date 2026-06-22
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            新增双电机 PWM 实现
 * 2026-06-19       1.1            zeh          改为裸机静态 GPIO 实现
 * 2026-06-19       2.0            zeh            重写为硬件 MCPWM + ISR 驱动（Phase 2）
 * 2026-06-21       2.1            zeh            修复惰性初始化中断重入窗口导致的崩溃
 * 2026-06-21       2.2            zeh        ISR 回调加 FPU 协处理器守卫，支持中断内浮点
 * 2026-06-21       2.3            zeh            诊断埋点：ISR 有效负载段耗时统计
 * 2026-06-21       2.4            zeh            修复 GPIO32/33/25 RTC domain 路由失效（B2 根因）
 * 2026-06-21       2.5            zeh            补 RTCIO 数字域切换(hal LL)修复 GPIO32/33/25 RTC 域未释放致 MCPWM 无输出
 * 2026-06-22       3.0            zeh            FOC 混合架构：新增 bm_vendor_pwm_hw_init_isr_only（仅挂 ISR）
 * 2026-06-22       3.1            zeh            清 B2 诊断埋点（DIAG_ISR 计时/diag_read_clear/diag_get_duty）
 * 2026-06-22       3.2            zeh            ISR 分频：新增 isr_decimate/isr_div_count 字段与 set_isr_decimate API，ADC+回调按 N 抽稀降 CPU 负载
 *
 */
#include "bm_vendor_pwm_esp32_idf.h"
#include "bm_vendor_esp32_idf_compat.h"
#include "bm_vendor_esp32_isr_fpu.h"
#include "bm_hal_instances_esp32wroom32e.h"
#include "bm_types.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hal/gpio_ll.h"
#include "hal/mcpwm_ll.h"
#include "hal/rtc_io_ll.h"
#include "soc/rtc_periph.h"
#include "soc/interrupts.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "esp_rom_gpio.h"      /**< esp_rom_gpio_pad_select_gpio / esp_rom_gpio_connect_out_signal（ROM 函数）。 */

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
/* B2 诊断①：临时降载波 20kHz→4kHz（prescale 4→20），把低边导通窗口拉宽到
 * ~125µs ≫ ADC 28µs 采样，验证"高频下采样窗口太窄→采不到相电流"假设。
 * 若降频后 ib raw 出现偏移/iq 跟上 → 根因坐实，进②重构采样；验证后恢复 4。 */
#define BM_VENDOR_PWM_TIMER_PRESCALE  20u
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
    /**
     * @brief ISR 内 FPU(CP0) 现场保存区（per-context 各一份，16 字节对齐）。
     *
     * 供 bm_vendor_esp32_isr_fpu_enter/exit 保存/恢复被打断代码的浮点现场，
     * 让本 ISR 回调内的浮点运算安全。每 MCPWM unit 独立持有，避免共享/嵌套。
     * 无 FPU 芯片上 BM_VENDOR_ESP32_ISR_FPU_SA_SIZE=1，仅占位、守卫为 no-op。
     */
    uint8_t cp0_sa[BM_VENDOR_ESP32_ISR_FPU_SA_SIZE] __attribute__((aligned(16)));
    /**
     * @brief ISR ADC 采样+回调的分频因子（CPU 预算调节）。
     *
     * 每 isr_decimate 次 TEZ 才执行一次 ADC oneshot 转换与后续回调链，降低
     * ADC 轮询对 CPU 的占用。默认 1（每拍均采样），与旧版行为完全兼容。
     * 清中断动作不受此分频影响（每拍必清）。
     * 由 bm_vendor_pwm_set_isr_decimate() 写入，ISR 只读。
     */
    uint32_t isr_decimate;
    /**
     * @brief ISR 分频计数器（每次 TEZ 自增，满 isr_decimate 后清零执行 ADC）。
     *
     * 仅在 bm_vendor_pwm_isr 内读写，单核确定性访问，无需同步原语。
     */
    uint32_t isr_div_count;
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
 * @brief MCPWM unit0/1 的 GPIO matrix 输出信号编号（PWMxA，operator 0/1/2）。
 *
 * 来自 ESP32 soc/gpio_sig_map.h（peripheral output signal index）：
 *   PWM0_OUT0A=84, PWM0_OUT1A=86, PWM0_OUT2A=88
 *   PWM1_OUT0A=90, PWM1_OUT1A=92, PWM1_OUT2A=94
 *
 * 修正（B2 bring-up 实测根因）：原表误填 {86,88,90}/{94,96,98}，整体偏了一个
 * operator——A 相被挂到 OP1A、C 相被挂到 PWM1 的 0A（M1 单元，M0 路径未初始化=
 * 死相），三相被错位路由，电流环算对的电压送不到桥臂（有 PWM 声却无绕组电流）。
 * 开环 app PWM 用 IDF 高层 mcpwm_new_generator 自动分配信号，故无此问题。
 */
static const int g_mcpwm_signal[BM_VENDOR_PWM_INSTANCE_COUNT][BM_VENDOR_PWM_PHASE_COUNT] = {
    { 84, 86, 88 },  /* MCPWM0 OP0A/OP1A/OP2A = PWM0_OUT0A/1A/2A */
    { 90, 92, 94 },  /* MCPWM1 OP0A/OP1A/OP2A = PWM1_OUT0A/1A/2A */
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
 * @brief ESP32 GPIO 编号 → RTCIO index 映射（仅本板用到的 RTC 复用脚）。
 *
 * GPIO25(DAC1)、GPIO32/33(XTAL_32K_P/N) 是 RTC 域复用脚，做数字输出前须切回
 * 数字域。值来自 soc/rtc_io_channel.h：RTCIO_GPIO25/32/33_CHANNEL = 6/9/8，
 * 系 ESP32 芯片物理固定映射（与板级布线无关）。非 RTC valid GPIO（如 M1 普通
 * 脚）返回 -1，调用方据此跳过 RTC 域释放。
 *
 * @param gpio_num GPIO 编号。
 * @return RTCIO index；非 RTC valid GPIO 返回 -1。
 */
static int bm_vendor_pwm_gpio_to_rtcio(uint32_t gpio_num)
{
    switch (gpio_num) {
    case 25u: return 6;  /* DAC1       → RTCIO6 */
    case 32u: return 9;  /* XTAL_32K_P → RTCIO9 */
    case 33u: return 8;  /* XTAL_32K_N → RTCIO8 */
    default:  return -1; /* 非 RTC 脚（含 M1 普通脚），无需 RTC 域释放 */
    }
}

/**
 * @brief 通过 GPIO matrix 将 GPIO 路由到 MCPWM 信号输出。
 *
 * 修复（B2 bring-up 根因）：M0 三相 GPIO32/33/25 均属 ESP32 RTC GPIO domain。
 * 原写法仅调 esp_rom_gpio_pad_select_gpio()（只设数字 IOMUX MCU_SEL 字段，
 * 不操作 RTCIO MUX_SEL 寄存器位），这三个 pad 的控制权仍留在 RTC 域，
 * 数字 GPIO matrix 侧的 func_out_sel_cfg 配置对物理引脚无效，导致 MCPWM
 * 波形无法到达引脚，桥臂无驱动，相电流始终为 0。
 *
 * 正确顺序（对齐 IDF gpio_config + mcpwm_new_generator 路径）：
 *   0. RTCIO 数字域切换            — 对 RTC valid GPIO（25/32/33）用 hal LL
 *      rtcio_ll_function_select(rtcio, DIGITAL) 将 pad 从 RTC 域释放回数字域
 *      （等价 IDF rtc_gpio_deinit 核心动作；vendor 层不依赖 driver 组件，故用
 *      LL 复刻，GPIO→RTCIO 映射见 bm_vendor_pwm_gpio_to_rtcio）。非 RTC GPIO
 *      （M1 普通脚）跳过，兼容。注意 esp_rom_gpio_pad_select_gpio 只设数字
 *      IOMUX，不切 RTC 域。
 *   1. esp_rom_gpio_pad_select_gpio() — 设数字 IOMUX（MCU_SEL 字段），将 pad
 *      接入数字 GPIO matrix 路径。
 *   2. gpio_ll_output_enable()        — 在 GPIO_ENABLE_REG 中使能输出方向。
 *   3. gpio_ll_input_disable()        — 关闭输入缓冲（PWM 纯输出场景）。
 *   4. gpio_ll_od_disable()           — 确保推挽输出（非开漏）。
 *   5. gpio_ll_set_drive_capability() — 40 mA 驱动能力，匹配栅驱电流需求。
 *   6. esp_rom_gpio_connect_out_signal() — 把 MCPWM 外设输出信号写入
 *      GPIO.func_out_sel_cfg[gpio_num]（等价于 gpio_matrix_out ROM 函数），同时
 *      自动将 IOMUX MCU_SEL 切到 func=2（GPIO matrix 路径），out_inv/oen_inv=false。
 *
 * @param gpio_num GPIO 编号（M0: 32/33/25；M1: 其它普通脚亦兼容）。
 * @param signal   MCPWM 输出信号编号（来自 g_mcpwm_signal）。
 */
static void bm_vendor_pwm_route_gpio(uint32_t gpio_num, int signal)
{
    int rtcio_num = bm_vendor_pwm_gpio_to_rtcio(gpio_num);

    /*
     * 步骤 0：将 RTC valid GPIO 从 RTC 域释放回数字域。
     * GPIO25（DAC1=RTCIO6）、GPIO32（XTAL_32K_P=RTCIO9）、GPIO33（XTAL_32K_N=RTCIO8）
     * 上电后 RTCIO MUX_SEL=1（RTC 控制），数字 GPIO matrix 侧配置对物理引脚无效。
     * rtcio_ll_function_select(DIGITAL) 清 MUX_SEL 位，将 pad 控制权交还数字域
     * （等价 IDF rtc_gpio_deinit 核心动作）。非 RTC 脚（rtcio_num<0，含 M1 普通脚）
     * 跳过，保持兼容。
     */
    if (rtcio_num >= 0) {
        rtcio_ll_function_select(rtcio_num, RTCIO_LL_FUNC_DIGITAL);
    }

    /*
     * 步骤 1：设数字 IOMUX，将 pad 接入 GPIO matrix 路径。
     * 注意：此函数只操作 IOMUX MCU_SEL 字段，不切 RTCIO MUX_SEL；
     * 步骤 0 已完成 RTC 域释放，此处仅完成数字侧 IOMUX 选择。
     */
    esp_rom_gpio_pad_select_gpio(gpio_num);

    /* 步骤 2-5：配置输出方向、关输入、推挽、驱动能力。 */
    gpio_ll_output_enable(&GPIO, gpio_num);
    gpio_ll_input_disable(&GPIO, gpio_num);
    gpio_ll_od_disable(&GPIO, gpio_num);
    gpio_ll_set_drive_capability(&GPIO, gpio_num, GPIO_DRIVE_CAP_3);

    /*
     * 步骤 6：连接 MCPWM 外设信号到 GPIO matrix 输出路径。
     * 内部写 GPIO.func_out_sel_cfg[gpio_num].func_sel = signal，并设
     * IOMUX MCU_SEL=2（走 GPIO matrix），out_inv=false，oen_inv=false。
     */
    esp_rom_gpio_connect_out_signal(gpio_num, (uint32_t)signal, false, false);
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
 *   1. 清除中断标志（每拍必做，不受分频影响）
 *   2. 分频判断：未到第 isr_decimate 拍则提前返回（跳过 ADC/FPU/回调）
 *   3. 软触发 ADC 采样（仅在该跑的拍执行）
 *   4. 调用 ADC 完成 HRT 回调
 *   5. 调用 PWM 更新 HRT 回调
 *
 * 铁律：清中断（步骤 1）绝对在分频 return（步骤 2）之前，否则漏清导致
 * 中断重入/挂死。ADC 采样+FPU+回调（步骤 3-5）仅在分频后的拍执行，
 * 降低 CPU 占用约 (1 - 1/isr_decimate) 倍。
 *
 * isr_decimate 默认为 1（每拍均采样），default 路径行为与旧版逐字相同。
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

    /*
     * 铁律步骤 1：每拍读取并清除 TEZ 中断（timer0 empty event = bit 3）。
     * 此操作必须在任何 return 之前完成，绝不可被分频跳过，否则中断标志
     * 残留→下一次 TEZ 触发时旧标志未清→重入/挂死。
     */
    status = mcpwm_ll_intr_get_status(hw);
    mcpwm_ll_intr_clear_status(hw, status);

    if ((status & MCPWM_LL_EVENT_TIMER_EMPTY(BM_VENDOR_PWM_TIMER_ID)) == 0u) {
        return;
    }

    /*
     * 步骤 2：分频抽稀——未到第 isr_decimate 拍跳过 ADC/FPU/回调。
     * isr_decimate 默认 1，++isr_div_count(=1) < 1 为假，每拍均执行（旧行为）。
     * isr_decimate=4 时，前 3 拍跳过（节省 ADC ~24µs×3/4 ≈ 56% CPU），
     * 第 4 拍（isr_div_count 归零后）执行完整 ADC+FPU+回调。
     */
    if (++ctx->isr_div_count < ctx->isr_decimate) {
        return;
    }
    ctx->isr_div_count = 0u;

    /* ADC 在 ISR 内软触发采样（ESP32 经典无 ETM 硬触发路径，仅在该跑的拍执行） */
    bm_vendor_adc_esp32_idf_isr_sample(motor_id);

    /*
     * 用户回调（adc_complete / update）可能跑浮点（如 FOC current_step）。
     * ESP 中断上下文默认禁用 FPU(CP0)，须用守卫开 CP0 并存/恢复被打断现场。
     * 两个回调共用一对 enter/exit，减少 CP0 存/恢复次数。
     * 守卫顺序铁律：开 CP0 → 存现场 → 跑浮点 → 复现场 → 还原 CPENABLE。
     */
    {
        unsigned cp_prev = bm_vendor_esp32_isr_fpu_enter(ctx->cp0_sa);

        /* ADC 完成回调（由 ADC 模块注册） */
        if (ctx->adc_complete_binding.callback != NULL) {
            ctx->adc_complete_binding.callback(ctx->adc_complete_binding.context);
        }

        /* PWM 更新回调 */
        if (ctx->update_binding.callback != NULL) {
            ctx->update_binding.callback(ctx->update_binding.context);
        }

        bm_vendor_esp32_isr_fpu_exit(ctx->cp0_sa, cp_prev);
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

    /* 分频因子默认 1（每拍均采样，与旧版等价）；调用方可在 init 后调
     * bm_vendor_pwm_set_isr_decimate 覆盖，ISR 使能前写入安全。 */
    ctx->isr_decimate  = 1u;
    ctx->isr_div_count = 0u;

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

    /*
     * 注册 ISR，但以 ESP_INTR_FLAG_INTRDISABLED 装入：alloc 后中断处于
     * 禁用态、绝不立即触发。配合最后再 esp_intr_enable，消除惰性初始化中断
     * 重入窗口——避免在 hw 尚未完成、ctx->initialized 仍为 0 时被 TEZ ISR
     * 抢入而重入 hw_init / esp_intr_alloc。
     */
    intr_src = (motor_id == 0u) ? ETS_PWM0_INTR_SOURCE : ETS_PWM1_INTR_SOURCE;
    ret = esp_intr_alloc(intr_src,
                         ESP_INTR_FLAG_LEVEL3 | ESP_INTR_FLAG_IRAM |
                             ESP_INTR_FLAG_INTRDISABLED,
                         bm_vendor_pwm_isr,
                         ctx,
                         &ctx->isr_handle);
    if (ret != 0) {
        return BM_ERR_IO;
    }

    /* 启动 timer（连续运行，不自停） */
    mcpwm_ll_timer_set_start_stop_command(hw, BM_VENDOR_PWM_TIMER_ID,
                                          MCPWM_TIMER_START_NO_STOP);

    /*
     * 此处硬件已全部配置就绪，先置 initialized=1 再放开中断：
     * 这样即便 TEZ 中断使能后立刻触发，ISR 调 set_duty→hw_init 也会因
     * initialized!=0 立即早返回，绝不会重入 esp_intr_alloc。
     */
    ctx->initialized = 1;

    /* 最后才使能 TEZ 事件源 + 放开 CPU 中断（顺序：标志置位 → 开中断）。 */
    mcpwm_ll_intr_enable(hw,
                         MCPWM_LL_EVENT_TIMER_EMPTY(BM_VENDOR_PWM_TIMER_ID),
                         true);
    (void)esp_intr_enable(ctx->isr_handle);

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
    /* 反相补偿：generator UP→HIGH/DOWN→LOW 下实际占空比=(peak-cmp)/peak，
     * 故 cmp=peak-duty 使入参 duty 正比于占空比（对齐开环 app PWM）。原 vendor
     * 缺此补偿→三相占空比整体反相、电压矢量反向。 */
    cmp_val = (uint32_t)BM_VENDOR_PWM_PEAK - cmp_val;
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

/**
 * @brief 设置 MCPWM TEZ ISR 内 ADC 采样+回调的分频因子（CPU 预算调节）。
 *
 * 每 n 次 TEZ 才执行一次 ADC oneshot 转换与后续 FPU 回调链：
 *   - n=1（默认）：每拍均执行，与修改前行为逐字相同（向后兼容）。
 *   - n=4（FOC 2.5kHz）：ADC 采样从 10kHz 降至 2.5kHz，CPU 占用降约 75%。
 *
 * 清中断操作始终每拍执行，不受分频影响（铁律，见 bm_vendor_pwm_isr 注释）。
 * 分频计数器 isr_div_count 随 isr_decimate 一同归零，确保下一拍计数从 0 重启。
 *
 * 调用时机：在 bm_vendor_pwm_hw_init_isr_only / bm_vendor_pwm_hw_init 之后、
 * TEZ ISR 开始出力之前（或运行时动态调整均安全，ISR 内只读 isr_decimate）。
 *
 * @param motor_id 电机编号（0 或 1）；越界时直接返回。
 * @param n        分频因子（≥1；传入 0 时视为 1，每拍均采样）。
 */
void bm_vendor_pwm_set_isr_decimate(uint32_t motor_id, uint32_t n)
{
    bm_vendor_pwm_context_t *ctx;

    if (motor_id >= BM_VENDOR_PWM_INSTANCE_COUNT) {
        return;
    }
    ctx = &g_pwm_context[motor_id];
    ctx->isr_decimate  = (n < 1u) ? 1u : n;
    ctx->isr_div_count = 0u;
}

/**
 * @brief FOC 混合架构：仅挂载 TEZ ISR，不配 timer/operator/GPIO。
 *
 * 在 app 层 hover_board_pwm_esp32_init + hover_board_pwm_esp32_enable(true) 之后、
 * hover_foc_axis_init（bind current_step 到 update callback）之前调用。
 *
 * 与原 bm_vendor_pwm_hw_init 的区别：
 *   - 跳过 bm_vendor_pwm_timer_init / bm_vendor_pwm_operator_init（app 已配）
 *   - 跳过 bm_vendor_pwm_route_gpio（app 已路由 GPIO）
 *   - 跳过 bm_vendor_pwm_set_en_pin（app hover_board_pwm_esp32_enable 已控制 M_EN）
 *   - 跳过 timer start（app 已启动 timer）
 *   - 保留：总线时钟使能 + ISR 注册 + TEZ 事件使能 + ctx->initialized 置位
 *
 * set_duty（bm_vendor_pwm_write_cmp）直写 comparator（量程 BOARD_FOC_PWM_MAX=1000），
 * 与 app 配好的 peak=1000 一致，反相补偿语义与 app set_duty 相同。
 *
 * @note 若 ctx->initialized 已非 0（重复调用），直接返回 BM_OK。
 *
 * @param motor_id 电机编号（0 或 1）。
 * @return BM_OK 成功；BM_ERR_INVALID 参数越界；BM_ERR_IO ISR 注册失败。
 */
int bm_vendor_pwm_hw_init_isr_only(uint32_t motor_id)
{
    bm_vendor_pwm_context_t *ctx;
    mcpwm_dev_t             *hw;
    int                      intr_src;
    int                      ret;

    if (motor_id >= BM_VENDOR_PWM_INSTANCE_COUNT) {
        return BM_ERR_INVALID;
    }
    ctx = &g_pwm_context[motor_id];
    if (ctx->initialized != 0) {
        return BM_OK;
    }

    /* 分频因子默认 1（每拍均采样，与旧版等价）；调用方可在 init 后调
     * bm_vendor_pwm_set_isr_decimate 覆盖，ISR 使能前写入安全。 */
    ctx->isr_decimate  = 1u;
    ctx->isr_div_count = 0u;

    hw = MCPWM_LL_GET_HW((int)motor_id);

    /*
     * 使能 MCPWM 总线时钟（app 层 IDF driver 路径可能已使能；此处幂等重入安全）。
     * 若不使能，LL 读写寄存器会访问未上电外设（undefined behavior）。
     */
    BM_PERIPH_RCC_ATOMIC_BEGIN
        mcpwm_ll_enable_bus_clock((int)motor_id, true);
        /* 不 reset_register：app 层已完整配置，reset 会清除已有 timer/operator 配置。 */
    BM_PERIPH_RCC_ATOMIC_END

    /*
     * 注册 TEZ ISR，以 ESP_INTR_FLAG_INTRDISABLED 装入：alloc 后中断处于
     * 禁用态，配合最后 esp_intr_enable 消除 init 期重入窗口。
     */
    intr_src = (motor_id == 0u) ? ETS_PWM0_INTR_SOURCE : ETS_PWM1_INTR_SOURCE;
    ret = esp_intr_alloc(intr_src,
                         ESP_INTR_FLAG_LEVEL3 | ESP_INTR_FLAG_IRAM |
                             ESP_INTR_FLAG_INTRDISABLED,
                         bm_vendor_pwm_isr,
                         ctx,
                         &ctx->isr_handle);
    if (ret != 0) {
        return BM_ERR_IO;
    }

    /*
     * 先置 initialized=1 再放开中断：保证 TEZ ISR 触发时 set_duty → hw_init
     * 的重入保护生效（initialized!=0 → 立即早返回）。
     */
    ctx->initialized = 1;

    /* 使能 TEZ 事件源（timer0 empty event）+ 放开 CPU 中断。 */
    mcpwm_ll_intr_enable(hw,
                         MCPWM_LL_EVENT_TIMER_EMPTY(BM_VENDOR_PWM_TIMER_ID),
                         true);
    (void)esp_intr_enable(ctx->isr_handle);

    return BM_OK;
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
