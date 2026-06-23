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
 * @version 2.4
 * @date 2026-06-22
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            新增双电机 ADC 实例
 * 2026-06-19       1.1            zeh            改为 ADC LL 裸机实现
 * 2026-06-19       2.0            zeh            Phase 2：改为 MCPWM ISR 触发采样
 * 2026-06-21       2.1            zeh            诊断埋点：ISR 内 ADC 采样时间与 wait 计数
 * 2026-06-22       2.2            zeh            清 B2 诊断埋点（DIAG_ADC 计时/diag_read_clear）
 * 2026-06-22       2.3            zeh            B3-S2a 降噪：每通道滑动中值-of-3（剔单拍脉冲毛刺，不增 ADC 转换次数）
 * 2026-06-22       2.4            zeh            B3-S2a 降噪二层：中值后串一极点 IIR 低通（α=1/4，压 SENSOR 脚连续底噪）
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
#include "hal/sar_ctrl_ll.h"   /**< SAR ADC 电源控制（force_xpd_sar）。 */
#endif

/** @brief 电机实例数量。 */
#define BM_VENDOR_ADC_INSTANCE_COUNT  2u
/** @brief 单个实例的采样 rank 数（ia / ib 两路）。 */
#define BM_VENDOR_ADC_RANK_COUNT      2u
/** @brief ISR 内 oneshot 转换的最大等待循环次数。 */
#define BM_VENDOR_ADC_POLL_LIMIT      2048u
/**
 * @brief 每通道 oneshot 连采次数（ISR 内同步采样）。
 *
 * B2 诊断②曾置 8（多次平均降噪），但 ESP32 经典 ADC 单次 oneshot 含轮询约
 * 12µs，8 次×双通道(ia/ib)=16 次使单次 isr_sample 耗时约 191µs（实测
 * adc_avg）。FOC 在 TEZ（计数谷底=三相全低边导通=唯一能测下桥采样电阻相电流
 * 的窗口）启动采样，而该低边窗口宽度受最高占空比相限制：4kHz 载波下约 93µs、
 * 20kHz 下仅约 18µs。191µs 的连采远超窗口，绝大多数采样点落到高边导通区
 * （下桥 MOSFET 关断、采样电阻无电流）→ 读回零电流偏置 1853 → iq 反馈恒≈0、
 * 电流环 vq 积分 windup 饱和（电机实有顿挫力矩，电流却测不到）。这正是
 * "降载波/倒序采样均无效、8 次平均后 raw 干净贴 1853" 的真因——多数点在高边
 * 读零电流被平均稀释，而非相电流真为 0。
 *
 * 故改回 1：单次双通道约 24µs，可收进谷底低边窗口（4kHz 下余量充足）。
 * 20kHz 治本（窗口仅 18µs）需把 ADC 移出 ISR / 改硬件触发，留待 B3。
 */
#define BM_VENDOR_ADC_AVG_N           1u

/**
 * @brief 滑动中值滤波开关（B3-S2a 降噪）：1=每通道对最近 3 拍取中值，0=直通。
 *
 * 针对 SENSOR_VP/VN 等噪声脚的**单拍脉冲毛刺**（实测 peak 飙 0.4-0.5A，远超
 * 真实 0.05A）：中值-of-3（跨连续控制拍）把单拍飞点剔除，3 个里的 1 个异常值
 * 被中值天然排除。**每拍仍只 1 次 oneshot 转换**（中值取历史而非一拍连采），
 * 故不增加 ISR 内 ADC 耗时、不重蹈 AVG_N=8 把采样挤出低边窗口的坑。
 * 群延迟约 1 拍（5kHz 下 0.2ms），对电流环可忽略。
 */
#define BM_VENDOR_ADC_MEDIAN3         1u

/**
 * @brief 三数中值（a+b+c-max-min，免排序，ISR 安全）。
 */
static inline uint16_t bm_vendor_adc_median3(uint16_t a, uint16_t b, uint16_t c)
{
    uint16_t mx = a > b ? a : b;
    uint16_t mn = a < b ? a : b;
    mx = mx > c ? mx : c;
    mn = mn < c ? mn : c;
    return (uint16_t)((uint32_t)a + (uint32_t)b + (uint32_t)c - mx - mn);
}

/**
 * @brief 一极点 IIR 低通移位量（B3-S2a 第二层降噪）：0=关，N>0 → α=1/2^N。
 *
 * 中值剔脉冲毛刺后，对 SENSOR_VP/VN 脚的**连续底噪**再做整数 EMA 低通：
 *   acc += x − (acc>>N)；y = acc>>N （acc 收敛到 x·2^N）。
 * shift=2（α=0.25）≈ 2× 降噪、τ≈0.7ms（~230Hz 截止）；电流环 Kp=0.1 保守、
 * 带宽低，该群延迟可接受。**代价是给电流反馈加相位滞后**，过深会削相位裕度→
 * 震荡，故起步 2、上板看 iq 跟踪稳定性再决定是否加深。整数运算、ISR 安全。
 */
#define BM_VENDOR_ADC_IIR_SHIFT       2u

typedef struct {
    /** @brief 电机编号（0/1）。 */
    uint32_t id;
} bm_vendor_adc_config_t;

typedef struct {
    /** @brief 是否已完成硬件初始化。 */
    int initialized;
    /** @brief ISR 内采样的原始 ADC 缓存值（中值滤波后，供控制环读取）。 */
    uint16_t cached[BM_VENDOR_ADC_RANK_COUNT];
#if BM_VENDOR_ADC_MEDIAN3
    /** @brief 每通道最近 3 拍原始采样环（中值滤波用）。 */
    uint16_t med_buf[BM_VENDOR_ADC_RANK_COUNT][3];
    /** @brief 每通道环形写指针（0..2）。 */
    uint8_t  med_pos[BM_VENDOR_ADC_RANK_COUNT];
    /** @brief 每通道是否已预热（首拍用同值填满 3 槽，避免起始偏斜）。 */
    uint8_t  med_primed[BM_VENDOR_ADC_RANK_COUNT];
#endif
#if BM_VENDOR_ADC_IIR_SHIFT > 0
    /** @brief 每通道 IIR 累加器（acc 收敛到 y·2^shift）。 */
    int32_t  iir_acc[BM_VENDOR_ADC_RANK_COUNT];
    /** @brief 每通道 IIR 是否已预热（首拍用 x<<shift 填充，避免起始爬升）。 */
    uint8_t  iir_primed[BM_VENDOR_ADC_RANK_COUNT];
#endif
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
 * @note 修复（ADC 衰减不生效根因）：
 *   ① 加 sar_ctrl_ll_set_power_mode(SAR_CTRL_LL_POWER_ON)：
 *      IDF 高层路径在 adc_oneshot_new_unit 时通过 sar_periph_ctrl_adc_oneshot_power_acquire
 *      将 SENS.sar_meas_wait2.force_xpd_sar 置 0x3（强制上电）；低层 RTC oneshot 路径若
 *      该字段处于默认 FSM 控制（0x0），SAR ADC 在 ISR 内功耗不足，读数会异常偏高（近满量程），
 *      与 DB_12 衰减失效（近 DB_0 饱和）表现一致——实测零电流 raw≈3930 即此原因。
 *   ② 加 adc_oneshot_ll_output_invert(ADC_UNIT_1, ADC_LL_DATA_INVERT_DEFAULT(0))：
 *      ESP32 上 ADC_LL_DATA_INVERT_DEFAULT=1，硬件原始 raw 需软件取反后才是正确线性量程
 *      输出。IDF 高层路径在每次 adc_oneshot_hal_setup 中都设 sar1_data_inv=1；低层路径
 *      若不设，读值为反转前的原始值（等价于 4095 - correct_raw），导致零点读数错误。
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

    /*
     * ① 强制 SAR ADC 上电（fix：缺此步 ISR 内读数异常偏高）。
     *    等价于 IDF 高层 sar_periph_ctrl_adc_oneshot_power_acquire()。
     *    两个电机实例共享同一物理 ADC1，多次调用幂等（写固定值 0x3）。
     */
    sar_ctrl_ll_set_power_mode(SAR_CTRL_LL_POWER_ON);

    /* 关闭 AMP（IDF 高层路径 adc_oneshot_hal_setup 对 ESP32 固定调用）。 */
    adc_ll_amp_disable();

    /* 选择 RTC 控制器（oneshot 软触发路径）。 */
    adc_ll_set_controller(ADC_UNIT_1, ADC_LL_CTRL_RTC);
    adc_oneshot_ll_set_output_bits(ADC_UNIT_1, ADC_BITWIDTH_12);

    /*
     * ② 数据反转（fix：ESP32 ADC LL 层读数为取反后的线性输出）。
     *    ADC_LL_DATA_INVERT_DEFAULT(periph)=1，高层每次 setup 都设此位；
     *    低层若不设，零电流读数为 4095-correct_raw 而非 correct_raw。
     */
    adc_oneshot_ll_output_invert(ADC_UNIT_1, ADC_LL_DATA_INVERT_DEFAULT(0));

    adc_oneshot_ll_enable(ADC_UNIT_1);

    channels = (ctx == &g_adc_context[0]) ? g_adc_channels_m0 : g_adc_channels_m1;
    for (rank = 0u; rank < BM_VENDOR_ADC_RANK_COUNT; ++rank) {
        /* 使用 ADC_ATTEN_DB_12（5.2.3 中 DB_11 已废弃）。 */
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
 * 由 MCPWM TEZ ISR 调用。每次调用对两路通道（ia/ib）倒序（ib 先、ia 后）转换，
 * 确保 θ_e=0 时 B/C 相电流（ib）落在 TEZ 谷底低边导通窗口。每通道 AVG_N=1 单采。
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
    /* 倒序采样（先 ib=rank1，后 ia=rank0）：θ_e=0 时 ib 排第一，确保落在谷底低边导通窗口。
     * cached[rank]↔通道映射不变。 */
    for (rank = BM_VENDOR_ADC_RANK_COUNT; rank-- > 0u; ) {
        /* 每通道采样前重设衰减，与 IDF 高层 adc_oneshot_hal_setup 行为对齐。 */
        adc_oneshot_ll_set_atten(ADC_UNIT_1,
                                 (adc_channel_t)channels[rank],
                                 ADC_ATTEN_DB_12);
        adc_oneshot_ll_set_channel(ADC_UNIT_1, channels[rank]);
        {
            uint32_t acc = 0u;
            uint32_t n;
            for (n = 0u; n < BM_VENDOR_ADC_AVG_N; ++n) {
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
                acc += (uint32_t)adc_oneshot_ll_get_raw_result(ADC_UNIT_1);
                adc_oneshot_ll_clear_event(ADC_LL_EVENT_ADC1_ONESHOT_DONE);
            }
            {
                /* 滤波级联：原始 → 中值（剔脉冲毛刺）→ IIR 低通（压连续底噪）。 */
                uint16_t filt = (uint16_t)(acc / BM_VENDOR_ADC_AVG_N);
#if BM_VENDOR_ADC_MEDIAN3
                /* 滑动中值-of-3（跨连续控制拍，剔除单拍脉冲毛刺）。 */
                if (!ctx->med_primed[rank]) {
                    /* 首拍：3 槽同值填满，median 即首值，避免起始偏斜。 */
                    ctx->med_buf[rank][0] = filt;
                    ctx->med_buf[rank][1] = filt;
                    ctx->med_buf[rank][2] = filt;
                    ctx->med_pos[rank]    = 0u;
                    ctx->med_primed[rank] = 1u;
                } else {
                    ctx->med_buf[rank][ctx->med_pos[rank]] = filt;
                    ctx->med_pos[rank] = (uint8_t)((ctx->med_pos[rank] + 1u) % 3u);
                }
                filt = bm_vendor_adc_median3(ctx->med_buf[rank][0],
                                             ctx->med_buf[rank][1],
                                             ctx->med_buf[rank][2]);
#endif
#if BM_VENDOR_ADC_IIR_SHIFT > 0
                /* 一极点整数 EMA 低通（acc 收敛到 filt·2^shift）。 */
                if (!ctx->iir_primed[rank]) {
                    ctx->iir_acc[rank]    = (int32_t)filt << BM_VENDOR_ADC_IIR_SHIFT;
                    ctx->iir_primed[rank] = 1u;
                } else {
                    ctx->iir_acc[rank] += (int32_t)filt - (ctx->iir_acc[rank] >> BM_VENDOR_ADC_IIR_SHIFT);
                }
                filt = (uint16_t)(ctx->iir_acc[rank] >> BM_VENDOR_ADC_IIR_SHIFT);
#endif
                ctx->cached[rank] = filt;
            }
        }
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
