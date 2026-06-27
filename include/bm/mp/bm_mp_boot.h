/**
 * @file bm_mp_boot.h
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @brief MP 闭源扩展公共 API · 需 bm_mp
 *
 * 协调 Bootstrap/Secondary 的多阶段 barrier、分区 CRC 校验与 IRQ release 门控。
 * 在 `BM_MP_BOOT_IRQ_RELEASE` 之前，`bm_hrt_start()` 与外设 IRQ 须返回
 * `BM_ERR_NOT_INIT`。
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
#ifndef BM_MP_BOOT_H
#define BM_MP_BOOT_H

#ifndef BM_CONFIG_MP_BOOT_BARRIER_TIMEOUT_US
#define BM_CONFIG_MP_BOOT_BARRIER_TIMEOUT_US  BM_CONFIG_BOOT_BARRIER_TIMEOUT_US
#endif

#if defined(BM_CONFIG_MP_BOOT_BARRIER_TIMEOUT_US) && \
    !defined(BM_CONFIG_BOOT_BARRIER_TIMEOUT_US)
#define BM_CONFIG_BOOT_BARRIER_TIMEOUT_US  BM_CONFIG_MP_BOOT_BARRIER_TIMEOUT_US
#endif

#include "bm/mp/bm_mp_types.h"
#include "bm/common/bm_types.h"

/** PERCPU 启动阶段（与共享区 boot_phase 原子字一致） */
typedef enum {
    BM_MP_BOOT_INIT = 0,
    BM_MP_BOOT_PARTITION_READY,
    BM_MP_BOOT_PROFILE_READY,
    BM_MP_BOOT_RUNTIME_READY,
    BM_MP_BOOT_INIT_READY,
    BM_MP_BOOT_START_READY,
    BM_MP_BOOT_IRQ_RELEASE,
    BM_MP_BOOT_FAILED
} bm_mp_boot_phase_t;

/**
 * @brief 格式化共享启动区
 *
 * @return BM_OK 成功
 */
int bm_mp_boot_format(void);

/**
 * @brief Bootstrap 核启动序列（分区 build、IPC format、释放从核）
 *
 * @return BM_OK 成功；负值为 fail-stop
 */
int bm_mp_boot_bootstrap_sequence(void);

/**
 * @brief 各核 attach 分区并初始化本核 event/ticker/IPC endpoint
 *
 * @return BM_OK 成功
 */
int bm_mp_boot_cpu_attach_and_init(void);

/**
 * @brief 全核 barrier，等待指定阶段
 *
 * @param phase 目标阶段
 * @param timeout_us 超时（微秒）；0 表示使用默认
 * @return BM_OK 成功；BM_ERR_TIMEOUT 超时；BM_ERR_INVALID 阶段失败
 */
int bm_mp_barrier_wait(bm_mp_boot_phase_t phase, uint32_t timeout_us);

/**
 * @brief 查询 IRQ release 门控是否已打开
 *
 * @return 非 0 表示已 release
 */
int bm_mp_boot_is_irq_released(void);

/**
 * @brief 要求 IRQ 已 release，否则返回 BM_ERR_NOT_INIT
 *
 * @return BM_OK 已 release；BM_ERR_NOT_INIT 尚未 release
 */
int bm_mp_boot_require_irq_released(void);

/**
 * @brief Bootstrap 打开 IRQ release 门控并广播 boot_phase
 */
void bm_mp_boot_release_irq(void);
void bm_mp_boot_report_failure(void);

/**
 * @brief 等待共享矩阵 boot_phase 达到目标阶段
 *
 * @param phase 目标阶段
 * @param timeout_us 超时微秒；0 表示不超时
 * @return BM_OK 成功；BM_ERR_TIMEOUT 超时
 */
int bm_mp_boot_wait_matrix_phase(bm_mp_boot_phase_t phase, uint32_t timeout_us);

/**
 * @brief Bootstrap 在 profile build 成功后广播 PROFILE_READY
 */
int bm_mp_boot_signal_profile_ready(void);

/**
 * @brief 当前核见到的共享 boot_epoch（attach 后有效）
 */
uint32_t bm_mp_boot_epoch(void);

#endif /* BM_MP_BOOT_H */
