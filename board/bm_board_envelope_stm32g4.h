/**
 * @file bm_board_envelope_stm32g4.h
 * @brief STM32G4 共享板级运行包络模板（多 Demo 复用）
 *
 * 电气/时序参数与 WCET 占位宏；sdk_stm32g4 实机构建时由板级 main 包含。
 * native_sim Demo 可不依赖本头文件。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            从 motor_foc 提炼共享模板
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

/** WCET 占位：电流环单次迭代上限（µs，实机 profiling 后填写） */
#define BOARD_WCET_CURRENT_LOOP_US      0u
/** WCET 占位：速度环单次迭代上限（µs） */
#define BOARD_WCET_SPEED_LOOP_US        0u
/** WCET 占位：ADC 采样+DMA 完成到算法入口（µs） */
#define BOARD_WCET_ADC_SAMPLE_TO_ALG_US 0u
/** WCET 占位：PWM 更新输出延迟（µs） */
#define BOARD_WCET_PWM_UPDATE_US        0u

#endif /* BM_BOARD_ENVELOPE_STM32G4_H */
