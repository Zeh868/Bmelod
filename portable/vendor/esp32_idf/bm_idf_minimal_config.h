/**
 * @file bm_idf_minimal_config.h
 * @brief Bmelod 独立 CMake 构建 ESP32 后端时的最小 sdkconfig 占位
 *
 * 在完整 ESP-IDF 工程中由 Kconfig 生成的 sdkconfig.h 覆盖。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            从 sdk_esp32_idf 迁入 vendor
 *
 */
#ifndef BM_IDF_MINIMAL_CONFIG_H
#define BM_IDF_MINIMAL_CONFIG_H

#ifndef CONFIG_IDF_TARGET_ESP32
#define CONFIG_IDF_TARGET_ESP32 1
#endif

#ifndef CONFIG_ESP_TASK_WDT_EN
#define CONFIG_ESP_TASK_WDT_EN 1
#endif

#ifndef CONFIG_ESP_CONSOLE_UART_NUM
#define CONFIG_ESP_CONSOLE_UART_NUM 0
#endif

#endif /* BM_IDF_MINIMAL_CONFIG_H */
