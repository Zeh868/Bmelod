/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_cpu_freq_esp32.c
 * @brief ESP32-WROOM-32E 真机 CPU 主频接口实现（查询 esp_clk，切频占位）
 * @maturity E1
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
 * @version 1.1
 * @date 2026-07-04
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-04       1.0            zeh            新增 esp32 真机主频接口实现（Task 4）
 * 2026-07-04       1.1            zeh            freq_set 由占位改安全版：命中当前频率即确认，
 *                                                 CONFIG_PM_ENABLE 时经 esp_pm 锁频，否则拒绝裸切
 *
 * 2026-08-01       1.1            Codex            补全中文 Doxygen 合规注释
 */
#include "hal/bm_hal_cpu.h"
#include "bm/common/bm_types.h"
#include "esp_private/esp_clk.h" /* esp_clk_cpu_freq()；IDF 5.x 私有头（上板 v5.2.3 核对：位于 esp_hw_support/include/esp_private） */
#include <stddef.h> /* NULL：独立 CMake 裸机路径下无传递包含，须显式引入 */
#ifdef CONFIG_PM_ENABLE
#include "esp_pm.h" /* esp_pm_configure()/esp_pm_config_t；本机无 IDF 未编译验证，字段名/头随 IDF 版本待上板核对 */
#endif

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
 * @brief 切换 CPU 主频（安全版）。
 *
 * 三条返回路径：
 *   1) `hz` 不在 `s_cpu_freq_points`（80/160/240 MHz）支持点集内 → `BM_ERR_INVALID`。
 *   2) `hz` 等于当前主频（`esp_clk_cpu_freq()` 运行期真值）→ `BM_OK`：
 *      上电已由 sdkconfig 定频到此，本调用只是框架侧的“确认/钉定”，
 *      无需真改硬件，是上电定频场景下的常规路径，无 footgun。
 *   3) `hz` 与当前主频不同、要求真改：
 *      - 若 `CONFIG_PM_ENABLE` 已定义，构造 `esp_pm_config_t`（`max_freq_mhz`
 *        与 `min_freq_mhz` 均设为 `hz/1000000`、`light_sleep_enable = false`），
 *        调用 `esp_pm_configure()` 用 ESP-IDF 官方 PM API 锁频（max==min 即关闭
 *        动态调频，不伤实时性）；成功返回 `BM_OK`，失败返回 `BM_ERR_NOT_SUPPORTED`。
 *      - 若未开 `CONFIG_PM_ENABLE`，返回 `BM_ERR_NOT_SUPPORTED`：无 PM 时不能安全
 *        裸改到不同频率——直接裸切 `rtc_clk` 不会同步更新 `g_ticks_per_us`/外设
 *        分频，会导致 delay、串口等时序错乱，故不做。
 *
 * @note 本机无 ESP-IDF 工具链，`esp_pm_config_t`/`esp_pm_configure()` 分支
 *       **未在本机编译验证**；`esp_pm_config_t` 字段名/头文件随 IDF 版本可能有别名
 *       （如旧版 `esp_pm_config_esp32_t`），消费方在 ESP-IDF 工程内构建时须按
 *       实际 IDF 版本核对。
 *
 * @param hz 目标频率（须为 freq_points 之一）
 * @return BM_OK 命中当前频率或锁频成功；BM_ERR_INVALID 不在支持点集内；
 *         BM_ERR_NOT_SUPPORTED 需要真改但无 PM 或锁频失败。
 */
int bm_hal_cpu_freq_set(uint32_t hz) {
    uint32_t i;
    int in_points = 0;

    for (i = 0u; i < (uint32_t)(sizeof s_cpu_freq_points / sizeof s_cpu_freq_points[0]); i++) {
        if (s_cpu_freq_points[i] == hz) {
            in_points = 1;
            break;
        }
    }
    if (!in_points) {
        return BM_ERR_INVALID;
    }

    if (hz == (uint32_t)esp_clk_cpu_freq()) {
        return BM_OK; /* 已在目标频率：上电已由 sdkconfig 定频到此，此处仅确认/钉定 */
    }

#ifdef CONFIG_PM_ENABLE
    {
        esp_pm_config_t cfg = { 0 };
        cfg.max_freq_mhz = (int)(hz / 1000000u);
        cfg.min_freq_mhz = (int)(hz / 1000000u);
        cfg.light_sleep_enable = false;
        if (esp_pm_configure(&cfg) == ESP_OK) {
            return BM_OK;
        }
        return BM_ERR_NOT_SUPPORTED;
    }
#else
    return BM_ERR_NOT_SUPPORTED; /* 无 PM 不能安全裸改到不同频率：rtc_clk 裸切不更新 ticks_per_us/外设分频 */
#endif
}
