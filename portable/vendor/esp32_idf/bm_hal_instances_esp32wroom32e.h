/**
 * @file bm_hal_instances_esp32wroom32e.h
 * @brief 灯哥 V4 平衡车 ESP32-WROOM-32E 板级引脚与电气常量
 *
 * 本头文件只保存与硬件绑定相关的宏定义，不直接依赖 ESP-IDF 头文件，
 * 以便在独立 CMake 语法检查和通用代码分析阶段也能被安全包含。
 *
 * @author zeh (china_qzh@163.com)
 * @version 2.0
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            从 sdk_esp32_idf 迁入 vendor
 * 2026-06-19       2.0            zeh            补全灯哥 V4 电机 HAL 板级宏与电气常量
 *
 */
#ifndef BM_HAL_INSTANCES_ESP32WROOM32E_H
#define BM_HAL_INSTANCES_ESP32WROOM32E_H

#ifndef CONFIG_ESP_CONSOLE_UART_NUM
/**
 * @brief ESP-IDF 控制台串口号占位宏
 *
 * 独立语法检查或未导入 sdkconfig.h 时，默认回落为 UART0。
 */
#define CONFIG_ESP_CONSOLE_UART_NUM 0
#endif

/**
 * @brief 控制台 UART 端口号
 *
 * 仅表示 UART 控制器编号，不隐含任何 GPIO 映射。
 */
#define BM_ESP32WROOM32E_UART_PORT  CONFIG_ESP_CONSOLE_UART_NUM

/**
 * @brief 系统 tick 定时器分辨率
 *
 * 与现有 ESP32 vendor 单例保持一致，单位为 Hz。
 */
#define BM_ESP32WROOM32E_TICK_TIMER_RESOLUTION_HZ  1000000u

/** @brief M0 三相 PWM：A 相 / IN1 对应 GPIO32。 */
#define BM_ESP32WROOM32E_M0_IN1_GPIO  32u
/** @brief M0 三相 PWM：B 相 / IN2 对应 GPIO33。 */
#define BM_ESP32WROOM32E_M0_IN2_GPIO  33u
/** @brief M0 三相 PWM：C 相 / IN3 对应 GPIO25。 */
#define BM_ESP32WROOM32E_M0_IN3_GPIO  25u

/** @brief M1 三相 PWM：A 相 / IN1 对应 GPIO26。 */
#define BM_ESP32WROOM32E_M1_IN1_GPIO  26u
/** @brief M1 三相 PWM：B 相 / IN2 对应 GPIO27。 */
#define BM_ESP32WROOM32E_M1_IN2_GPIO  27u
/** @brief M1 三相 PWM：C 相 / IN3 对应 GPIO14。 */
#define BM_ESP32WROOM32E_M1_IN3_GPIO  14u

/**
 * @brief 电机总使能 GPIO
 *
 * 高有效；进入安全态时必须拉低。
 */
#define BM_ESP32WROOM32E_M_EN_GPIO  12u

/** @brief M0 电流采样 ia（ADC1_CH3，对应 GPIO39）。 */
#define BM_ESP32WROOM32E_M0_CS1_GPIO  39u
/** @brief M0 电流采样 ib（ADC1_CH0，对应 GPIO36）。 */
#define BM_ESP32WROOM32E_M0_CS2_GPIO  36u
/** @brief M1 电流采样 ia（ADC1_CH7，对应 GPIO35）。 */
#define BM_ESP32WROOM32E_M1_CS1_GPIO  35u
/** @brief M1 电流采样 ib（ADC1_CH6，对应 GPIO34）。 */
#define BM_ESP32WROOM32E_M1_CS2_GPIO  34u

/** @brief 母线电压采样（ADC2_CH4，对应 GPIO13）。 */
#define BM_ESP32WROOM32E_VIN_MEA_GPIO  13u

/** @brief 编码器 0 SDA0：GPIO19。 */
#define BM_ESP32WROOM32E_ENCODER0_SDA_GPIO  19u
/** @brief 编码器 0 SCL0：GPIO18。 */
#define BM_ESP32WROOM32E_ENCODER0_SCL_GPIO  18u
/** @brief 编码器 0 预留 CS0：GPIO22，I2C 模式下保持输入或上拉预留。 */
#define BM_ESP32WROOM32E_ENCODER0_CS_GPIO   22u

/**
 * @brief 编码器 1 SDA1：模块脚 37 对应 GPIO23。
 *
 * 仅允许按 GPIO23 配置；不可误写为 ESP32 GPIO37。
 */
#define BM_ESP32WROOM32E_ENCODER1_SDA_GPIO  23u
/** @brief 编码器 1 SCL1：GPIO5。 */
#define BM_ESP32WROOM32E_ENCODER1_SCL_GPIO    5u
/** @brief 编码器 1 预留 CS1：GPIO21，I2C 模式下保持输入或上拉预留。 */
#define BM_ESP32WROOM32E_ENCODER1_CS_GPIO    21u

/** @brief RGB 状态灯 WS2812B 数据脚。 */
#define BM_ESP32WROOM32E_RGB_GPIO  2u

/** @brief 调试串口 0 TX。 */
#define BM_ESP32WROOM32E_UART0_TX_GPIO  1u
/** @brief 调试串口 0 RX。 */
#define BM_ESP32WROOM32E_UART0_RX_GPIO  3u

/** @brief 扩展 UART1 TX。 */
#define BM_ESP32WROOM32E_UART1_TX_GPIO  17u
/** @brief 扩展 UART1 RX。 */
#define BM_ESP32WROOM32E_UART1_RX_GPIO  16u

/** @brief 扩展 IO0。 */
#define BM_ESP32WROOM32E_EXT_IO0_GPIO  15u
/** @brief 扩展 IO1。 */
#define BM_ESP32WROOM32E_EXT_IO1_GPIO   4u

/** @brief 电流环触发频率（Hz）。 */
#define BOARD_FOC_CURRENT_HZ  10000u
/** @brief 速度环触发频率（Hz）。 */
#define BOARD_FOC_SPEED_HZ    1000u
/** @brief PWM 载波频率（Hz），中心对齐。 */
#define BOARD_FOC_PWM_HZ      20000u
/** @brief PWM 满量程比较值。 */
#define BOARD_FOC_PWM_MAX     1000u
/** @brief 标称母线电压（V）。 */
#define BOARD_FOC_VBUS_V      24.0f
/** @brief 母线分压比。 */
#define BOARD_FOC_VBUS_DIV    8.5f
/** @brief 电流 ADC 标定比例。 */
#define BOARD_FOC_CURRENT_ADC_SCALE  1000.0f
/** @brief 电机极对数占位值。 */
#define BOARD_FOC_POLE_PAIRS  7.0f
/** @brief AS5600 RAW ANGLE 12bit 分辨率。 */
#define BOARD_FOC_ENCODER_CPR  4096u
/** @brief 电流采样 rank 0，对应 ia。 */
#define BOARD_FOC_ADC_RANK_IA  0u
/** @brief 电流采样 rank 1，对应 ib。 */
#define BOARD_FOC_ADC_RANK_IB  1u

#endif /* BM_HAL_INSTANCES_ESP32WROOM32E_H */
