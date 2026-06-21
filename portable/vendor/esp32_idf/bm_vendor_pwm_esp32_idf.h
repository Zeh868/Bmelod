/**
 * @file bm_vendor_pwm_esp32_idf.h
 * @brief ESP32-WROOM-32E 板级 PWM 实例声明（Phase 2：MCPWM 硬件驱动）
 *
 * FOC 混合架构（2026-06-22 起）：
 *   - MCPWM 硬件配置（timer/operator/GPIO）由 app 层 hover_board_pwm_esp32 负责。
 *   - vendor 层通过 bm_vendor_pwm_hw_init_isr_only 仅挂载 TEZ ISR（不配 timer）。
 *   - 运行时 set_duty 通过 MCPWM_LL_GET_HW + mcpwm_ll_operator_set_compare_value
 *     直写 app 已配好的 comparator（量程 BOARD_FOC_PWM_MAX=1000）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 3.1
 * @date 2026-06-22
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            新增 M0/M1 双电机 PWM 实例
 * 2026-06-19       2.0            zeh            Phase 2：改为 MCPWM 硬件驱动接口
 * 2026-06-22       3.0            zeh            FOC 混合架构：新增 hw_init_isr_only
 * 2026-06-22       3.1            zeh            清 B2 诊断埋点（diag_read_clear/diag_get_duty）
 *
 */
#ifndef BM_VENDOR_PWM_ESP32_IDF_H
#define BM_VENDOR_PWM_ESP32_IDF_H

#include "bm_hal_pwm.h"
#include <stdint.h>

/** @brief M0 电机 PWM 实例（MCPWM unit0）。 */
extern const bm_hal_pwm_t bm_hal_pwm_m0;
/** @brief M1 电机 PWM 实例（MCPWM unit1）。 */
extern const bm_hal_pwm_t bm_hal_pwm_m1;

/**
 * @brief FOC 混合架构：仅挂载 TEZ ISR（不配 timer/operator/GPIO）。
 *
 * 在 app 层 hover_board_pwm_esp32_init + hover_board_pwm_esp32_enable(true) 之后调用。
 * app 层已完成 MCPWM 单元完整配置并启动 timer；本函数仅完成：
 *   1. 使能 MCPWM 总线时钟（若尚未使能）
 *   2. esp_intr_alloc 注册 TEZ ISR（ESP_INTR_FLAG_INTRDISABLED）
 *   3. 设置 ctx->initialized=1，置 MCPWM_LL_EVENT_TIMER_EMPTY 事件源
 *   4. esp_intr_enable 放开 CPU 中断
 *
 * 对指定 motor_id（0 或 1）调用，M0/M1 各调一次。
 * 调用后 vendor set_duty 可通过 LL 直写 comparator；TEZ ISR 触发 ADC 采样 + 回调链。
 *
 * @param motor_id 电机编号（0 或 1）。
 * @return 0 成功；非 0 失败（BM_ERR_IO / BM_ERR_INVALID）。
 */
int bm_vendor_pwm_hw_init_isr_only(uint32_t motor_id);

/**
 * @brief 将 ADC 完成回调绑定到指定电机的 MCPWM ISR。
 *
 * 由 ADC 驱动在初始化或回调注册时调用；MCPWM TEZ ISR 在采样后触发该回调。
 *
 * @param motor_id 电机编号（0/1）。
 * @param binding  HRT 绑定；NULL 表示清除。
 */
void bm_vendor_pwm_esp32_idf_bind_adc_complete(uint32_t motor_id,
                                                const bm_hal_hrt_binding_t *binding);

#endif /* BM_VENDOR_PWM_ESP32_IDF_H */
