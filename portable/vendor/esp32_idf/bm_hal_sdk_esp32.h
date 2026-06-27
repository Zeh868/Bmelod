/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_sdk_esp32.h
 * @brief ESP32 裸机后端的 IDF 底层头入口
 *
 * 这里只汇聚裸机后端需要的错误码、版本宏与底层头文件，
 * 不引入任何调度器或高级外设包装。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            迁入 vendor
 * 2026-06-15       1.1            zeh            增加版本判定
 * 2026-06-19       1.2            zeh          去除高层驱动入口
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

#endif /* BM_HAL_SDK_ESP32_H */
