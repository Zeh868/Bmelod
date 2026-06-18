/**
 * @file bm_hal_cpu.h
 * @brief CPU 抽象接口
 *
 * 提供 CPU ID 查询、Bootstrap/Secondary 判定、从核启动及内存屏障原语。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 *
 */
#ifndef BM_HAL_CPU_H
#define BM_HAL_CPU_H

#include "bm/common/bm_types.h"

void     bm_hal_cpu_init(void);
uint32_t bm_hal_cpu_id(void);
int      bm_hal_cpu_is_bootstrap(void);
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

#endif /* BM_HAL_CPU_H */
