/**
 * @file bm_hal_sdk_esp32.h
 * @brief ESP-IDF 头文件入口（需 IDF_PATH 或 ESP-IDF 组件构建）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            从 sdk_esp32_idf 迁入 vendor
 * 2026-06-15       1.1            zeh            增加 ESP-IDF 版本判定头
 *
 */
#ifndef BM_HAL_SDK_ESP32_H
#define BM_HAL_SDK_ESP32_H

#if defined(__has_include) && __has_include("sdkconfig.h")
#include "sdkconfig.h"
#else
#include "bm_idf_minimal_config.h"
#endif

#include "esp_err.h"
#include "esp_idf_version.h"
#include "driver/gptimer.h"
#include "driver/uart.h"
#include "esp_task_wdt.h"

#endif /* BM_HAL_SDK_ESP32_H */
