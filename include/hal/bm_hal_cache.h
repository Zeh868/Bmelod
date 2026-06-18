/**
 * @file bm_hal_cache.h
 * @brief DMA / IPC payload cache maintenance 与 device barrier
 *
 * 真机 Port：设 `BM_HAL_CACHE_IS_NOOP=0` 并实现 `bm_hal_cache_clean` /
 * `bm_hal_cache_invalidate`（可替换本文件或在 portable 目录提供强符号实现）。
 *
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
#ifndef BM_HAL_CACHE_H
#define BM_HAL_CACHE_H

#include "bm/common/bm_atomic_ipc.h"
#include "bm/common/bm_types.h"

#include <stdint.h>

#ifndef BM_HAL_CACHE_IS_NOOP
/**
 * 1 = 无 D-cache / native_sim（仅 fence）；0 = 真机有 D-cache，须 Port 实现 clean/invalidate。
 * 板级可在 bm_config.h 或 CMake 中覆盖。
 */
#define BM_HAL_CACHE_IS_NOOP  1
#endif

/**
 * @brief 将 CPU 脏 cache 写回内存（TX / producer commit 前）
 *
 * @param addr 起始地址
 * @param len  字节长度
 * @return BM_OK 成功；负值为平台错误
 */
int bm_hal_cache_clean(const volatile void *addr, uint32_t len);

/**
 * @brief 使 CPU cache 失效以读取设备/DMA 新数据（RX / consumer 前）
 *
 * @param addr 起始地址
 * @param len  字节长度
 * @return BM_OK 成功；负值为平台错误
 */
int bm_hal_cache_invalidate(const volatile void *addr, uint32_t len);

/**
 * @brief 设备可见性屏障（DMA doorbell 前后）
 */
void bm_hal_device_barrier(void);

/**
 * @brief 当前平台是否无 D-cache（或 cache 维护为 no-op）
 */
int bm_hal_cache_is_noop(void);

/**
 * @brief CPU 写完 payload 后、设备/他核可见前（有 D-cache 则 clean，否则 release fence）
 */
static inline int bm_hal_cache_payload_publish(const void *addr, uint32_t len) {
    if (!addr || len == 0u) {
        return BM_OK;
    }
    if (bm_hal_cache_is_noop()) {
        bm_atomic_ipc_fence_release();
        return BM_OK;
    }
    return bm_hal_cache_clean(addr, len);
}

/**
 * @brief CPU 读 payload 前（有 D-cache 则 invalidate，否则 acquire fence）
 */
static inline int bm_hal_cache_payload_consume(const void *addr, uint32_t len) {
    if (!addr || len == 0u) {
        return BM_OK;
    }
    if (bm_hal_cache_is_noop()) {
        bm_atomic_ipc_fence_acquire();
        return BM_OK;
    }
    return bm_hal_cache_invalidate(addr, len);
}

#endif /* BM_HAL_CACHE_H */
