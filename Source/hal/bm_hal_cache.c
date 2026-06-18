/**
 * @file bm_hal_cache.c
 * @brief cache maintenance 默认实现（native_sim / 无 D-cache 平台）
 *
 * `BM_HAL_CACHE_IS_NOOP=1`（默认）：`bm_hal_cache_is_noop()` 为真，stream 仅走 fence。
 * `BM_HAL_CACHE_IS_NOOP=0`：须在 Port 中提供真实 clean/invalidate，勿仅依赖本桩。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-06-15       1.1            zeh            非 NOOP 禁止编译默认 fence-only 桩
 *
 */
#include "hal/bm_hal_cache.h"
#include "bm/common/bm_atomic_ipc.h"
#include "bm_types.h"

int bm_hal_cache_is_noop(void) {
#if BM_HAL_CACHE_IS_NOOP
    return 1;
#else
    return 0;
#endif
}

/*
 * 确定性流式 / 按 CPU 路由安全编译期守卫：
 * BM_HAL_CACHE_IS_NOOP=0 时，Port 必须替换本文件并提供真实 clean/invalidate
 * 实现。若仍编译此 fence-only 桩代码，构建失败——防止静默 DMA 数据损坏。
 */
#if !BM_HAL_CACHE_IS_NOOP
#error "BM_HAL_CACHE_IS_NOOP=0 requires a port-provided cache maintenance implementation." \
       " Do NOT compile this fence-only stub; replace bm_hal_cache.c in the port layer."
#endif

int bm_hal_cache_clean(const volatile void *addr, uint32_t len) {
    (void)addr;
    (void)len;
    bm_atomic_ipc_fence_release();
    return BM_OK;
}

int bm_hal_cache_invalidate(const volatile void *addr, uint32_t len) {
    (void)addr;
    (void)len;
    bm_atomic_ipc_fence_acquire();
    return BM_OK;
}

void bm_hal_device_barrier(void) {
    /*
     * DMA doorbell 须用全屏障：确保之前的 store 和之后的 load
     * 均不跨越此屏障。ARM 上为 DSB/DMB，x86 上为 mfence。
     * bm_atomic_ipc_fence_full() 在 ARM 上映射到 DMB SY / DSB，
     * 在 x86 上映射到 mfence，在 C11 上映射到 memory_order_seq_cst。
     */
    bm_atomic_ipc_fence_full();
}
