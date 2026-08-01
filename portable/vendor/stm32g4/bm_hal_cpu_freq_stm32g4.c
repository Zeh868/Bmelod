/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_cpu_freq_stm32g4.c
 * @brief STM32G4 CPU 主频接口（真机规则：SDK 时钟树拥有主频，本版不接管）
 * @maturity E1
 *
 * 实现规则（对齐 01-HAL契约与移植要点.md §各类 port 的实现规则）：
 *   - bm_hal_cpu_freq_hz()：读运行期真值 SystemCoreClock（由 Cube
 *     system_stm32g4xx.c 的 SystemCoreClockUpdate 维护）；
 *   - bm_hal_cpu_freq_points()：声明单点 {170MHz}（G474 标称主频）；
 *   - bm_hal_cpu_freq_set()：恒 BM_ERR_NOT_SUPPORTED——真实切频待后续 PM
 *     子系统接入，本版不占位。
 *
 * 去重约定：桩 Source/hal/bm_hal_cpu_stub.c 据 BM_HAL_CPU_HAS_PORT_FREQ 让出
 * 这三个符号；该宏由 pack（portable/packs/sdk_stm32g4/CMakeLists.txt）直接
 * 加到 bm_hal 目标的编译定义上（bm_hal 不依赖本 vendor，vendor 侧的 PUBLIC
 * 定义到不了桩所在编译单元，机制见 01 文档 133-148 行）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（STM32G474xB 移植）
 *
 * 2026-08-01       1.0            Codex            补全中文 Doxygen 合规注释
 */
#include "hal/bm_hal_cpu.h"
#include "bm_hal_instances_stm32g4.h"

#include <stddef.h>
#include <stdint.h>

#include "stm32g4xx.h"

/** @brief 单频率点表（G474 标称主频，与 BM_STM32G4_CPU_FREQ_HZ 一致）。 */
static const uint32_t s_cpu_freq_points[] = { BM_STM32G4_CPU_FREQ_HZ };

/**
 * @brief 查询当前主频（Hz），运行期真值。
 * @return SystemCoreClock 当前值。
 */
uint32_t bm_hal_cpu_freq_hz(void)
{
    return SystemCoreClock;
}

/**
 * @brief 声明本芯片支持的频率点集合（单点 170MHz，不支持 DVFS）。
 * @param[out] points 指向 port 内部静态点表。
 * @param[out] count  点表元素个数。
 * @return BM_OK 成功；BM_ERR_INVALID 参数为 NULL。
 */
int bm_hal_cpu_freq_points(const uint32_t **points, uint32_t *count)
{
    if (points == NULL || count == NULL) {
        return BM_ERR_INVALID;
    }
    *points = s_cpu_freq_points;
    *count  = (uint32_t)(sizeof s_cpu_freq_points / sizeof s_cpu_freq_points[0]);
    return BM_OK;
}

/**
 * @brief 切换主频（本版不支持：SDK 时钟树拥有主频，真实切频待 PM 子系统）。
 * @param hz 目标频率（未使用）。
 * @return 恒 BM_ERR_NOT_SUPPORTED。
 */
int bm_hal_cpu_freq_set(uint32_t hz)
{
    (void)hz;
    return BM_ERR_NOT_SUPPORTED;
}
