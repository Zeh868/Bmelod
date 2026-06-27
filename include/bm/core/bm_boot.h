/**
 * @file bm_boot.h
 * @brief RTD 启动状态机
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
#ifndef BM_BOOT_H
#define BM_BOOT_H

#include "bm/core/bm_ipc.h"

typedef enum {
    BOOT_INIT = 0,
    BOOT_RELEASE,
    BOOT_READY
} bm_boot_state_t;

/**
 * @brief 格式化启动状态区。
 */
int  bm_boot_format(bm_ipc_shared_t *ipc);

/**
 * @brief 执行 Bootstrap 启动序列。
 */
int  bm_boot_bootstrap_sequence(bm_ipc_shared_t *ipc);

/**
 * @brief 执行 Secondary 启动序列。
 */
int  bm_boot_secondary_sequence(bm_ipc_shared_t *ipc);

/**
 * @brief 读取当前启动状态。
 */
int  bm_boot_get_state(const bm_ipc_shared_t *ipc);

/**
 * @brief 查询是否允许放开 IRQ。
 */
int  bm_boot_is_ready_for_irqs(const bm_ipc_shared_t *ipc);

#endif /* BM_BOOT_H */
