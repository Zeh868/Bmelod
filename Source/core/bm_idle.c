/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_idle.c
 * @brief 应用层空闲钩子默认实现（路线图 #8 省电/空闲钩子）
 *
 * 提供 bm_idle() 的 weak 默认实现：
 * - ARM 目标（检测 __arm__ / __aarch64__）：执行内联 WFI，CPU 停振等待中断。
 * - 其他平台（宿主 x86、RISC-V 仿真等）：空操作，直接返回。
 *
 * 应用只需定义同名强符号即可覆盖默认行为，无需任何配置。
 * 若平台不支持弱符号（如 MSVC），可在 bm_config.h 定义
 * BM_CONFIG_IDLE_EXTERNAL_HOOK=1 并提供外部实现，
 * 本文件不再生成占位实现。
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
#include "bm/common/bm_idle.h"

/*
 * 架构 WFI 原语（自包含，不依赖 portmacro 头文件路径）：
 *   - ARM Cortex-M / Cortex-A（__arm__ 或 __aarch64__）：内联 wfi；
 *   - 其余平台（宿主 x86、RISC-V 仿真、MSVC 桩）：退化为空操作。
 * 此定义仅在本文件内有效，不对外暴露。
 */
#if defined(__arm__) || defined(__aarch64__) || defined(__ARM_ARCH)
/** ARM 平台：等待中断（Wait For Interrupt） */
#define BM_IDLE_YIELD_IMPL() __asm volatile ("wfi")
#else
/** 非 ARM 平台：空操作 */
#define BM_IDLE_YIELD_IMPL() ((void)0)
#endif

/*
 * 不支持弱符号的平台由外部提供实现：
 *   在 bm_config.h 定义 BM_CONFIG_IDLE_EXTERNAL_HOOK=1 可跳过本段。
 */
#if !defined(BM_CONFIG_IDLE_EXTERNAL_HOOK) || !(BM_CONFIG_IDLE_EXTERNAL_HOOK)

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
/**
 * @brief 空闲钩子默认实现（weak）
 *
 * ARM 目标执行 WFI；宿主仿真直接返回（空操作）。
 * 应用可提供同名强符号覆盖以实现自定义低功耗策略。
 */
void bm_idle(void) {
    BM_IDLE_YIELD_IMPL();
}

#endif /* !BM_CONFIG_IDLE_EXTERNAL_HOOK */
