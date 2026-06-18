/**
 * @file bm_hal_memory.c
 * @brief 内存屏障 HAL 分发层（契约 → driver API）
 *
 * 有 BM_DRV_HAS_BACKEND 时转发至 Port driver API；否则提供带 fence 的桩实现。
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
#include "bm_drv_memory.h"
#include "bm_hal_memory.h"
#include "bm/common/bm_atomic_ipc.h"

#ifdef BM_DRV_HAS_BACKEND
extern const struct bm_memory_driver_api bm_drv_memory_api;
#define BM_MEMORY_DRV (&bm_drv_memory_api)
#else
/*
 * 桩实现必须提供编译器屏障以防止重排。
 * 仅空函数体会被编译器优化掉，导致跨语句重排。
 * 使用 atomic_thread_fence 确保最小屏障语义。
 */
static void memory_stub_release(void) {
    bm_atomic_ipc_fence_release();
}

static void memory_stub_full(void) {
    bm_atomic_ipc_fence_full();
}

static const struct bm_memory_driver_api memory_stub = {
    memory_stub_release,
    memory_stub_full,
};

#define BM_MEMORY_DRV (&memory_stub)
#endif

void bm_memory_barrier_release(void) {
    if (BM_MEMORY_DRV->barrier_release) {
        BM_MEMORY_DRV->barrier_release();
    }
}

void bm_memory_barrier_full(void) {
    if (BM_MEMORY_DRV->barrier_full) {
        BM_MEMORY_DRV->barrier_full();
    }
}
