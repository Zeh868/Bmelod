/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_profile_epoch.c
 * @brief profile 代际查询注册表实现
 *
 * 统一使用按 CPU 索引的函数指针表：单核时数组长度为 1，多核时按核独立
 * 注册与查询。这样共享 TU 不再依赖 `BM_MP_MULTICORE` 分支，避免单/多核
 * 行为在条件编译上出现分叉。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            per-CPU 存储，SMP 安全
 * 2026-06-15       1.2            zeh            代际查询 hook 注册后冻结
 * 2026-06-19       1.3            zeh            去除单/多核条件分支，统一实现
 *
 */
#include "bm/common/bm_profile_epoch.h"
#include "bm/common/bm_atomic_ipc.h"
#include "bm/core/bm_cpu_local.h"
#include "hal/bm_hal_cpu.h"

#include <stddef.h>

/*
 * 代际查询函数表：每个 CPU 独立注册自己的 profile 代际查询函数。
 *
 * 单核时数组长度退化为 1；多核时每核独立注册与查询。注册阶段使用
 * store-release 发布函数指针，查询阶段使用 load-acquire 读取指针，确保
 * 弱内存序平台上的可见性与指针值完整性。
 */
static bm_profile_epoch_query_fn_t volatile s_query[BM_CONFIG_CPU_COUNT];

/**
 * @brief 注册当前 CPU 的 profile 代际查询函数
 *
 * 首次注册后不可替换；再次以不同指针注册将被忽略。
 *
 * @param fn 查询函数指针
 */
void bm_profile_epoch_register(bm_profile_epoch_query_fn_t fn) {
    uint32_t cpu = bm_hal_cpu_id();

    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return;
    }
    if (s_query[cpu] != NULL && s_query[cpu] != fn) {
        return;
    }
    bm_atomic_ipc_fence_release();
    s_query[cpu] = fn;
}

/**
 * @brief 读取当前 CPU 的 profile 代际
 *
 * @return 当前代际；未注册时返回 0
 */
uint32_t bm_profile_epoch_current(void) {
    uint32_t cpu = bm_hal_cpu_id();
    bm_profile_epoch_query_fn_t fn;

    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return 0u;
    }
    fn = s_query[cpu];
    bm_atomic_ipc_fence_acquire();
    return fn ? fn() : 0u;
}
