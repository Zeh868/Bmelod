/**
 * @file bm_hal_uptime.c
 * @brief 全框架统一单调时钟核心层实现
 *
 * 薄包装后端 `bm_hal_uptime_ns_raw()`，暴露公共 API
 * `bm_uptime_ns()` / `bm_uptime_us()`。
 *
 * 无 backend 时提供返回 0 的桩实现，保证链接不中断。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-26       1.0            zeh            正式发布（路线图 #9 时间基统一 1a）
 *
 */
#include "hal/bm_hal_uptime.h"
#include "bm/common/bm_uptime.h"

#ifdef BM_DRV_HAS_BACKEND

uint64_t bm_uptime_ns(void) {
    return bm_hal_uptime_ns_raw();
}

uint64_t bm_uptime_us(void) {
    return bm_hal_uptime_ns_raw() / 1000u;
}

#else /* !BM_DRV_HAS_BACKEND — 全桩，供无硬件单元测试链接 */

/**
 * @brief 无 backend 桩：始终返回 0
 *
 * 仅供不挂任何 portable 后端（如 stub/CI 环境）时链接不报错。
 * 业务代码不应依赖此返回值。
 */
uint64_t bm_uptime_ns(void) {
    return 0u;
}

/**
 * @brief 无 backend 桩：始终返回 0
 */
uint64_t bm_uptime_us(void) {
    return 0u;
}

#endif /* BM_DRV_HAS_BACKEND */
