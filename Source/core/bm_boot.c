/**
 * @file bm_boot.c
 * @brief RTD 启动状态机实现
 *
 * P0 版本为 native_sim 单核 stub：Bootstrap 直接设置 boot ready 标志并进入
 * BOOT_READY；Secondary 在 BOOT_RELEASE 后设置 srt_ready。
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
#include "bm/core/bm_boot.h"
#include "bm/common/bm_types.h"
#include "bm/common/bm_atomic_ipc.h"
#include "hal/bm_hal_cpu.h"

int bm_boot_format(bm_ipc_shared_t *ipc) {
    if (!ipc) {
        return BM_ERR_INVALID;
    }
    bm_atomic_ipc_store_u32(&ipc->boot_state, BOOT_INIT);
    bm_atomic_ipc_store_u32(&ipc->rt_ready, 0);
    bm_atomic_ipc_store_u32(&ipc->srt_ready, 0);
    return BM_OK;
}

int bm_boot_bootstrap_sequence(bm_ipc_shared_t *ipc) {
    if (!ipc) {
        return BM_ERR_INVALID;
    }
    bm_atomic_ipc_store_u32(&ipc->boot_state, BOOT_RELEASE);
    bm_atomic_ipc_store_u32(&ipc->srt_ready, 1);
    bm_atomic_ipc_store_u32(&ipc->rt_ready, 1);
    bm_atomic_ipc_store_u32(&ipc->boot_state, BOOT_READY);
    return BM_OK;
}

int bm_boot_secondary_sequence(bm_ipc_shared_t *ipc) {
    if (!ipc) {
        return BM_ERR_INVALID;
    }
    bm_atomic_ipc_store_u32(&ipc->srt_ready, 1);
    bm_atomic_ipc_store_u32(&ipc->boot_state, BOOT_READY);
    return BM_OK;
}

int bm_boot_get_state(const bm_ipc_shared_t *ipc) {
    if (!ipc) {
        return BOOT_INIT;
    }
    return (int)bm_atomic_ipc_load_u32(
        (bm_atomic_ipc_u32_t *)&ipc->boot_state);
}

int bm_boot_is_ready_for_irqs(const bm_ipc_shared_t *ipc) {
    if (!ipc) {
        return 0;
    }
    return bm_atomic_ipc_load_u32(
               (bm_atomic_ipc_u32_t *)&ipc->rt_ready) &&
           bm_atomic_ipc_load_u32(
               (bm_atomic_ipc_u32_t *)&ipc->srt_ready);
}
