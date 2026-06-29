/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_sync_hal_stub.c
 * @brief 同步域 HAL 空实现（未链接平台 HAL 时使用）
 *
 * GCC/Clang 下为弱符号，可被平台 HAL 覆盖；其他工具链依赖构建系统二选一链接。
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-12
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-12       1.1            zeh            弱符号桩与 trigger 日志降级
 *
 */
#include "bm_sync.h"
#include "bm_log.h"

#if defined(__GNUC__) || defined(__clang__)
#define BM_SYNC_HAL_STUB_WEAK __attribute__((weak))
#else
#define BM_SYNC_HAL_STUB_WEAK
#endif

/**
 * @brief 同步域 HAL 配置桩实现（未链接平台 HAL）
 *
 * @param domain 同步域描述符指针
 * @return BM_ERR_NOT_INIT 始终返回未初始化
 */
BM_SYNC_HAL_STUB_WEAK
int bm_sync_hal_configure(const bm_sync_domain_t *domain) {
    (void)domain;
    BM_LOGW("sync_hal", "stub configure: no HAL linked");
    return BM_ERR_NOT_INIT;
}

/**
 * @brief 同步域 HAL 武装桩实现（未链接平台 HAL）
 *
 * @param domain 同步域描述符指针
 * @return BM_ERR_NOT_INIT 始终返回未初始化
 */
BM_SYNC_HAL_STUB_WEAK
int bm_sync_hal_arm(const bm_sync_domain_t *domain) {
    (void)domain;
    BM_LOGW("sync_hal", "stub arm: no HAL linked");
    return BM_ERR_NOT_INIT;
}

/**
 * @brief 同步域 HAL 触发桩实现（未链接平台 HAL）
 *
 * @param domain 同步域描述符指针
 * @return BM_ERR_NOT_INIT 始终返回未初始化
 */
BM_SYNC_HAL_STUB_WEAK
int bm_sync_hal_trigger(const bm_sync_domain_t *domain) {
    (void)domain;
    BM_LOGD("sync_hal", "stub trigger: no HAL linked");
    return BM_ERR_NOT_INIT;
}

/**
 * @brief 同步域 HAL 安全停止桩实现（空操作）
 *
 * @param domain 同步域描述符指针
 */
BM_SYNC_HAL_STUB_WEAK
void bm_sync_hal_safe_stop(const bm_sync_domain_t *domain) {
    (void)domain;
    BM_LOGD("sync_hal", "stub safe_stop: no HAL linked");
}
