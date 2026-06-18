/**
 * @file bm_hal_instances_esp32wroom32e.h
 * @brief 灯哥平衡车主控板 ESP32-WROOM-32E 模组参考映射（ESP-IDF 驱动）
 *
 * 当前仅提供安全默认：控制台 UART0（`CONFIG_ESP_CONSOLE_UART_NUM`）、
 * GPTimer 系统 tick（1 MHz 分辨率）、Task WDT。
 * 电机驱动、IMU、PWM、ADC 等板级引脚须待用户提供原理图/引脚表后补全。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            从 sdk_esp32_idf 迁入 vendor
 * 2026-06-15       1.1            zeh            明确灯哥平衡车主控板范围与待补外设
 *
 */
#ifndef BM_HAL_INSTANCES_ESP32WROOM32E_H
#define BM_HAL_INSTANCES_ESP32WROOM32E_H

#include "bm_hal_sdk_esp32.h"

/** 控制台 UART（ROM / 默认下载串口，不臆造 GPIO 映射） */
#define BM_ESP32WROOM32E_UART_PORT  CONFIG_ESP_CONSOLE_UART_NUM

/** 系统 tick：GPTimer（替代手写 Timer Group 寄存器） */
#define BM_ESP32WROOM32E_TICK_TIMER_RESOLUTION_HZ  1000000u

#endif /* BM_HAL_INSTANCES_ESP32WROOM32E_H */
