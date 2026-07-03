/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_cpu_freq_esp32.c
 * @brief ESP32-WROOM-32E 真机 CPU 主频接口实现（查询 esp_clk，切频占位）
 *
 * 为 `hal/bm_hal_cpu.h` 声明的 freq_hz/freq_points/freq_set 三接口提供
 * ESP32 真机语义：freq_hz 直接查 ESP-IDF 运行期真值（`esp_clk_cpu_freq()`），
 * freq_points 声明 ESP32 支持的三档主频（80/160/240 MHz），freq_set 暂占位
 * 返回 `BM_ERR_NOT_SUPPORTED`（主频切换由 ESP-IDF 时钟树拥有，待后续 PM
 * 模块接入 `esp_pm`/`rtc_clk` 后才具备真实切频能力）。
 *
 * @note `esp_clk_cpu_freq()` 所属头文件随 IDF 版本演进（早期版本位于
 *       `esp_clk.h`，较新版本可能迁移至 `esp_clk_tree.h`/`esp32/clk.h`
 *       等路径）。本仓无 ESP-IDF 工具链，此文件**未在本机编译验证**，
 *       消费方在 ESP-IDF 工程内构建时须按实际 IDF 版本核对该头文件名，
 *       如有出入需替换本文件顶部的 include。
 *
 * @note 本文件所在的 `bm_vendor_esp32_idf` 静态库上虽挂了
 *       `target_compile_definitions(... PUBLIC BM_HAL_CPU_HAS_PORT_FREQ)`，
 *       但该 PUBLIC 定义只对本 vendor 目标的消费方生效，桩
 *       `bm_hal_cpu_stub.c` 属 `bm_hal` 目标、`bm_hal` 并不依赖本 vendor
 *       （依赖方向是 vendor→bm_config），故这条定义**到不了**桩的编译单元。
 *       桩要让出本文件的 freq 三函数，须由消费方 ESP-IDF 工程**全局**定义
 *       `BM_HAL_CPU_HAS_PORT_FREQ`（idf.py 组件级 `-D`，或经
 *       `bm_config.h` 的 include 链，如在其 `bm_config_app.h` 里
 *       `#define`），使其在编译 `bm_hal_cpu_stub.c` 时可见；否则本文件与
 *       桩的 freq 三函数会重复定义、链接失败。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-04
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-04       1.0            zeh            新增 esp32 真机主频接口实现（Task 4）
 *
 */
#include "hal/bm_hal_cpu.h"
#include "bm/common/bm_types.h"
#include "esp_clk.h" /* esp_clk_cpu_freq()；实际头随 IDF 版本可能是 esp_clk_tree/clk.h，待上板核对 */

/** @brief ESP32-WROOM-32E 支持的主频点表（80/160/240 MHz，与 IDF sdkconfig 三档一致） */
static const uint32_t s_cpu_freq_points[] = { 80000000u, 160000000u, 240000000u };

/**
 * @brief 查询当前 CPU 主频（Hz），直接读 ESP-IDF 时钟树运行期真值。
 * @return 当前主频，单位 Hz。
 */
uint32_t bm_hal_cpu_freq_hz(void) {
    return (uint32_t)esp_clk_cpu_freq();
}

/**
 * @brief 声明 ESP32 支持的频率点集合（80/160/240 MHz）。
 * @param points [out] 指向内部静态点表首元素
 * @param count  [out] 点数
 * @return BM_OK 成功；BM_ERR_INVALID 入参为 NULL。
 */
int bm_hal_cpu_freq_points(const uint32_t **points, uint32_t *count) {
    if ((points == NULL) || (count == NULL)) {
        return BM_ERR_INVALID;
    }
    *points = s_cpu_freq_points;
    *count = (uint32_t)(sizeof s_cpu_freq_points / sizeof s_cpu_freq_points[0]);
    return BM_OK;
}

/**
 * @brief 切换 CPU 主频（占位）。
 * @param hz 目标频率（须为 freq_points 之一）
 * @return 恒为 BM_ERR_NOT_SUPPORTED：主频由 ESP-IDF 时钟树拥有，
 *         真实切频待后续 PM 模块接入 `esp_pm`/`rtc_clk`。
 */
int bm_hal_cpu_freq_set(uint32_t hz) {
    (void)hz;
    return BM_ERR_NOT_SUPPORTED; /* 时钟由 ESP-IDF 拥有，切频待 PM 接 esp_pm/rtc_clk */
}
