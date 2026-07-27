/**
 * @file bm_component_common.h
 * @brief component 层公共小工具（跨组件复用，避免样板重复）
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-07-02
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-02       0.1            zeh            提取 adc_to_current（原三处组件各自重复实现）
 * 2026-07-02       0.2            zeh            新增 BM_COMPONENT_PUBLISH_TELEMETRY 遥测发布宏
 *                                                （原 component 层 20+ 处重复样板）
 * 2026-07-27       0.3            zeh            新增组件四段式公共宏
 *                                                RETURN_IF_NULL / VALIDATE_POSITIVE_FLOAT /
 *                                                INIT / RESET_TELEMETRY
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_COMPONENT_COMMON_H
#define BM_COMPONENT_COMMON_H

#include <bm_config.h>

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 16 位 ADC 原始计数转有符号电流（安培）
 *
 * 以 BM_ADC_MIDPOINT_16BIT 为零点，(raw - 中点) / scale 得到电流值；
 * scale 为电流采样标定系数（A/count 的倒数，由各组件按分流电阻/放大
 * 增益换算得出）。原三处组件（motor_foc_sensorless.c /
 * motor_foc_sensored.c / motor_current_sense.c）各自维护一份逐字相同
 * 的实现，此处统一为公共 helper。
 *
 * @param scale 电流标定系数（须非零，调用方保证）
 * @param raw   ADC 原始计数（16 位）
 * @return 换算后的电流值（安培）
 */
static inline float bm_component_adc_to_current(float scale, uint16_t raw) {
    return ((float)((int32_t)raw - BM_ADC_MIDPOINT_16BIT)) / scale;
}

/**
 * @brief 泛型遥测发布：回调非 NULL 时调用，否则静默跳过
 *
 * component 层 20+ 处重复
 * `if (axis->resources.publish_telemetry != NULL) { axis->resources.publish_telemetry(axis->resources.publish_telemetry_user, &tel); }`
 * 样板。各组件的 publish_telemetry 回调签名各自持有独立 typedef——
 * 形状一致（`void (*)(void *user, const T *telemetry)`），但载荷类型
 * T 因组件而异；C 语言下不同 T 的函数指针类型互不兼容，若用单一
 * 函数指针形参承接，调用点需做跨类型强制转换（有未定义行为风险，
 * 且丢失编译期类型检查）。故此处不做成函数、改用函数式宏：在展开处
 * 按调用方实际类型直接调用，编译器仍能对 payload 类型做静态检查，
 * 只消除重复的 NULL 判断与调用样板本身。
 *
 * @param axis_ptr 组件实例/轴指针（须有 resources.publish_telemetry /
 *                 resources.publish_telemetry_user 字段，且指针本身非 NULL）
 * @param tel_ptr  遥测负载指针，类型须与 resources.publish_telemetry
 *                 回调形参匹配
 */
#define BM_COMPONENT_PUBLISH_TELEMETRY(axis_ptr, tel_ptr) \
    do { \
        if ((axis_ptr)->resources.publish_telemetry != NULL) { \
            (axis_ptr)->resources.publish_telemetry( \
                (axis_ptr)->resources.publish_telemetry_user, (tel_ptr)); \
        } \
    } while (0)

/**
 * @brief 参数为空时直接返回 BM_ERR_INVALID
 *
 * 收敛 `if (x == NULL) { return BM_ERR_INVALID; }` 样板。
 *
 * @param p 待检查指针
 */
#define BM_COMPONENT_RETURN_IF_NULL(p) \
    do { \
        if ((p) == NULL) { return BM_ERR_INVALID; } \
    } while (0)

/**
 * @brief 正浮点参数校验：<= 0 时返回 BM_ERR_INVALID
 *
 * 用于采样周期、截止频率、时间常数等必须 >0 的配置项校验。
 *
 * @param x 待检查浮点值
 */
#define BM_COMPONENT_VALIDATE_POSITIVE_FLOAT(x) \
    do { \
        if ((x) <= 0.0f) { return BM_ERR_INVALID; } \
    } while (0)

/**
 * @brief 组件四段式 init 公共骨架
 *
 * 执行：NULL 检查 → validate_config(&axis->config) → reset(axis) → BM_OK。
 * 适用于 config/state/resources/axis 结构的标准组件。
 *
 * @param axis         组件轴实例指针
 * @param validate_fn  bm_<name>_validate_config 函数
 * @param reset_fn     bm_<name>_reset 函数
 */
#define BM_COMPONENT_INIT(axis, validate_fn, reset_fn) \
    do { \
        if ((axis) == NULL || (validate_fn)(&(axis)->config) != BM_OK) { \
            return BM_ERR_INVALID; \
        } \
        (reset_fn)(axis); \
        return BM_OK; \
    } while (0)

/**
 * @brief 复位组件 state.telemetry 为零初值
 *
 * 收敛 reset/safe_stop 中重复出现的 `memset(&axis->state.telemetry, 0, ...)`。
 *
 * @param axis_ptr 组件轴实例指针（须含 state.telemetry 字段）
 */
#define BM_COMPONENT_RESET_TELEMETRY(axis_ptr) \
    do { \
        memset(&(axis_ptr)->state.telemetry, 0, \
               sizeof((axis_ptr)->state.telemetry)); \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* BM_COMPONENT_COMMON_H */
