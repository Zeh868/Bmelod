/**
 * @file bm_board_envelope_stm32g4.h
 * @brief STM32G4 有感 FOC 板级运行包络（用户填写/标定）
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始模板
 *
 * 本文件定义 motor_foc_sensored 实机移植时的电气/时序参数占位。
 * native_sim Demo 不依赖此头文件；sdk_stm32g4 构建时由板级 main 包含。
 */
#ifndef BM_BOARD_ENVELOPE_STM32G4_H
#define BM_BOARD_ENVELOPE_STM32G4_H

/** 电流环频率（Hz） */
#define BOARD_FOC_CURRENT_HZ          10000u
/** 速度环频率（Hz） */
#define BOARD_FOC_SPEED_HZ            1000u
/** PWM 频率（Hz），须为电流环整数倍 */
#define BOARD_FOC_PWM_HZ              20000u
/** 母线电压（V） */
#define BOARD_FOC_VBUS_V              24.0f
/** 相电阻（Ω） */
#define BOARD_FOC_PHASE_R_OHM           0.5f
/** 极对数 */
#define BOARD_FOC_POLE_PAIRS            4.0f
/** 编码器线数（counts/rev） */
#define BOARD_FOC_ENCODER_CPR         4096u
/** 双电阻采样：ADC rank ia / ib */
#define BOARD_FOC_ADC_RANK_IA           0u
#define BOARD_FOC_ADC_RANK_IB           1u
/** PWM 满量程计数值 */
#define BOARD_FOC_PWM_MAX              1000u
/** 电流 ADC 标定：A → raw 比例（实机标定后更新） */
#define BOARD_FOC_CURRENT_ADC_SCALE  1000.0f

#endif /* BM_BOARD_ENVELOPE_STM32G4_H */
