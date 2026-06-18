/**
 * @file bm_hal_cap.h
 * @brief HAL 能力位查询（stream IRQ 屏蔽等确定性契约）
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
#ifndef BM_HAL_CAP_H
#define BM_HAL_CAP_H

#include <stdint.h>

/** 平台可在 commit 路径屏蔽 stream/DMA 相关 IRQ（同核临界区可证明）
 *
 *  由 port 层在编译期通过 BM_HAL_CRITICAL_MASKS_STREAM_IRQ 宏显式声明。
 *  不再从 BM_DRV_HAS_BACKEND 推断——存在后端 ≠ 一定能屏蔽 Stream IRQ。 */
#define BM_HAL_CAP_CRITICAL_MASKS_STREAM_IRQ  (1u << 0)

/** 平台提供 payload cache clean/invalidate 原语 */
#define BM_HAL_CAP_STREAM_CACHE_MAINT          (1u << 1)

/**
 * @brief 查询当前平台 HAL 能力掩码
 */
uint32_t bm_hal_cap_query(void);

/**
 * @brief hard RT stream profile 要求的 capability 子集是否满足
 */
int bm_hal_cap_stream_profile_ok(void);

#endif /* BM_HAL_CAP_H */
