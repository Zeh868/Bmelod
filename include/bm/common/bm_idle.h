/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_idle.h
 * @brief 应用层空闲钩子（路线图 #8 省电/空闲钩子）
 *
 * 当主循环无任务可做时，应用调用 bm_idle() 告知框架 CPU 空闲。
 * 默认实现为 weak 符号，在支持 WFI 的 ARM 目标上执行 wfi 进入低功耗等待，
 * 在宿主仿真（native_sim）上退化为空操作。
 *
 * 应用或 vendor 可通过覆盖同名强符号来提供自定义低功耗策略：
 *
 * @code
 * // 自定义实现示例（覆盖 weak 默认）
 * void bm_idle(void) {
 *     my_pmu_enter_stop_mode();
 * }
 * @endcode
 *
 * @note 不支持弱符号的平台（如 MSVC）可定义
 *       `BM_CONFIG_IDLE_EXTERNAL_HOOK=1` 并提供外部实现，
 *       框架不再生成默认占位实现。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-26       1.0            zeh            新增（路线图 #8 省电/空闲钩子）
 *
 */
#ifndef BM_IDLE_H
#define BM_IDLE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 主循环空闲钩子：CPU 进入低功耗等待直至下次中断
 *
 * 供裸机主循环在无事可做时调用。默认实现（weak）：
 * - ARM Cortex-M/A：执行 WFI，CPU 停振等待中断；
 * - 宿主仿真（native_sim / PC 单元测试）：空操作，直接返回。
 *
 * 应用或 BSP 可覆盖本函数以实现深度睡眠、电源域管理等策略。
 */
void bm_idle(void);

#ifdef __cplusplus
}
#endif

#endif /* BM_IDLE_H */
