/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_cpu.h
 * @brief CPU 抽象接口
 *
 * 提供 CPU ID 查询、Bootstrap/Secondary 判定、从核启动及内存屏障原语。
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-03
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-07-03       1.1            zeh            新增 CPU 主频接口 freq_hz/freq_points/freq_set
 * 2026-07-03       1.2            zeh            新增开机对账 freq_check/freq_check_config
 * 2026-08-01       1.2            zeh           补全 Doxygen 合规注释
 *
 */
#ifndef BM_HAL_CPU_H
#define BM_HAL_CPU_H

#include "bm/common/bm_types.h"

/** @brief 初始化当前 CPU 的 HAL 支持 */
void     bm_hal_cpu_init(void);
/** @brief 获取当前逻辑 CPU 编号 @return 当前 CPU 编号 */
uint32_t bm_hal_cpu_id(void);
/** @brief 判断当前 CPU 是否为引导核 @return 引导核返回 1，否则返回 0 */
int      bm_hal_cpu_is_bootstrap(void);
/** @brief 启动从核 @param entry_pc 从核入口地址 @return BM_OK 成功；负值表示平台错误 */
int      bm_hal_cpu_boot_secondary(uintptr_t entry_pc);

/**
 * @brief 等待所有已启动的从核线程结束（native_sim / 宿主仿真）
 *
 * 真机无 OS 线程时通常立即返回 BM_OK。
 *
 * @return BM_OK 全部从核已退出；BM_ERR_TIMEOUT 等待超时（若平台支持）
 */
int      bm_hal_cpu_join_secondary(void);

/**
 * @brief CPU 让步原语（忙等待循环中降功耗/让总线）
 *
 * 在忙等待循环中调用以降低总线争用或让出 SMT 资源。
 * - native_sim / POSIX: sched_yield() 或 Sleep(0)
 * - Cortex-M: __WFE() 或 __NOP()
 * - 无平台后端: 空操作
 *
 * 不得在 ISR 内调用（ISR 不可 yield）。
 */
void     bm_hal_cpu_yield(void);

/**
 * @brief 查询当前 CPU 主频（Hz）。每个 CPU 提供者（桩/port）必实现。
 * @return 当前主频，单位 Hz。
 */
uint32_t bm_hal_cpu_freq_hz(void);

/**
 * @brief 声明本芯片支持的频率点集合（DVFS 候选，移植时由提供者声明）。
 * @param points [out] 指向内部静态点表首元素
 * @param count  [out] 点数（>=1）
 * @return BM_OK；BM_ERR_INVALID 入参为 NULL。
 * @note 不支持 DVFS 的提供者返回单点。sim/qemu 镜像 config 点集；供未来 PM 择档与开机对账。
 */
int bm_hal_cpu_freq_points(const uint32_t **points, uint32_t *count);

/**
 * @brief 将 CPU 主频切到某频率点（占位机制，供后续 PM 调用）。
 * @param hz 目标频率（须为 freq_points 之一）
 * @return BM_OK 成功；BM_ERR_INVALID hz 不在支持集内；
 *         BM_ERR_NOT_SUPPORTED 真机/SDK 拥有时钟的 port 暂未接 PM。
 * @note sim/qemu 仅记录（无真实时钟）；真机 esp32 返回 NOT_SUPPORTED，待 PM 接真实切频。
 */
int bm_hal_cpu_freq_set(uint32_t hz);

/**
 * @brief 校验 config 声明的主频/频率点与 port 运行期真值一致（纯逻辑，便于单测）。
 * @param cfg_freq   BM_CONFIG_CPU_FREQ_HZ；0=未声明，直接返回 BM_OK
 * @param cfg_points config DVFS 点数组（可 NULL）
 * @param cfg_n      config 点数
 * @param port_freq  bm_hal_cpu_freq_hz()
 * @param port_points bm_hal_cpu_freq_points() 的表
 * @param port_n     port 点数
 * @return BM_OK 一致；BM_ERR_INVALID 主频不符/某 cfg 点不在 port 支持集/ref 不在 port 支持集
 */
int bm_hal_cpu_freq_check(uint32_t cfg_freq, const uint32_t *cfg_points, uint32_t cfg_n,
                          uint32_t port_freq, const uint32_t *port_points, uint32_t port_n);

/**
 * @brief 开机对账门面：把 config 宏与 port 查询喂给 bm_hal_cpu_freq_check。
 * @return 见 bm_hal_cpu_freq_check。应用/未来 PM 可在启动时可选调用（不强制）。
 */
int bm_hal_cpu_freq_check_config(void);

#endif /* BM_HAL_CPU_H */
