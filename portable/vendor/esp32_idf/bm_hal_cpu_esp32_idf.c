/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_cpu_esp32_idf.c
 * @brief ESP32-WROOM-32E 真实双核 CPU HAL 实现（核号探测 + bootstrap 判定）。
 * @maturity E1
 *
 * 补齐 `hal/bm_hal_cpu.h` 声明的 CPU 抽象接口。此前 esp32_idf vendor 目录
 * 从未提供这些符号的真实实现，`app_firmware` 真机构建实际链接的是
 * `Source/hal/bm_hal_cpu_stub.c`（`bm_hal_cpu_id()` 恒返回 0，等价单核）——
 * `bm_module`/`bm_ticker`/`bm_exec`/`bm_resource`/`bm_log`/`bm_profile_epoch`/
 * `bm_ultra` 等多个子系统的每核路由均以 `bm_hal_cpu_id()`/`bm_hal_cpu_is_bootstrap()`
 * 为依据，真机双核部署前必须补齐（Plan B 缺口②）。
 *
 * `esp_cpu_get_core_id()`（`esp_cpu.h`，IDF 5.2.3 稳定 API，替代已废弃的
 * `xPortGetCoreID()`）返回调用方所在物理核（PRO_CPU=0/APP_CPU=1），与
 * `motor_safe_test_main.c:2022/2025` 里 `xTaskCreatePinnedToCore` 的核编号
 * 语义一致。
 *
 * 只在 `NOT BM_HAL_USE_CPU_STUB` 时参与编译（见本目录 CMakeLists.txt），避免
 * 与 `Source/hal/bm_hal_cpu_stub.c` 同时链接产生重复符号——任何仍保留默认
 * `BM_HAL_USE_CPU_STUB=ON` 的既有配置（含 `motor_safe_test` 若未来引入本框架
 * 模块系统）不受本文件影响，零行为变化。
 *
 * @note 关闭 `BM_HAL_USE_CPU_STUB` 会整体丢弃 `bm_hal_cpu_stub.c`，而该桩不仅
 *       提供 `bm_hal_cpu_id()`，还唯一提供 `bm_hal_cpu_init`/`is_bootstrap`/
 *       `boot_secondary`/`join_secondary`/`yield`（`bm_wdg`/`bm_log`/
 *       `bm_hal_console` 等消费 `is_bootstrap`）。故本文件须补齐这一整套非 freq
 *       接口以闭合链接，与 native/qemu_esp32_smp 等其它「关桩」后端的完整 CPU
 *       HAL 做法一致；CPU 主频三接口仍由本目录 `bm_hal_cpu_freq_esp32.c`
 *       （配合 `BM_HAL_CPU_HAS_PORT_FREQ`）提供，不在此重复。ESP-IDF 自身负责
 *       APP_CPU（core1）的上电引导（Plan B Task 8 经 `xTaskCreatePinnedToCore`
 *       落核），框架不自行引导从核，故 `boot_secondary` 返回 NOT_SUPPORTED、
 *       `join_secondary` 直接 BM_OK，与 `bm_hal_cpu_native.c` 取向一致。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-11
 *
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-07-11       1.0            zeh            Plan B Task1：补齐真机双核 CPU-ID HAL（缺口②）
 * 2026-08-01       1.0            zeh            补全中文 Doxygen 合规注释
 */
#include "hal/bm_hal_cpu.h"
#include "bm_config.h"
#include "esp_cpu.h"

void bm_hal_cpu_init(void)
{
}

uint32_t bm_hal_cpu_id(void)
{
    return (uint32_t)esp_cpu_get_core_id();
}

int bm_hal_cpu_is_bootstrap(void)
{
    return (bm_hal_cpu_id() == 0u) ? 1 : 0;
}

int bm_hal_cpu_boot_secondary(uintptr_t entry_pc)
{
    /* ESP-IDF/FreeRTOS 自身负责 APP_CPU 上电引导；框架不自行引导从核。 */
    (void)entry_pc;
    return BM_ERR_NOT_SUPPORTED;
}

int bm_hal_cpu_join_secondary(void)
{
    return BM_OK;
}

void bm_hal_cpu_yield(void)
{
}
