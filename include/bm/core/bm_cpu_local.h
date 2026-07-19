/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_cpu_local.h
 * @brief CPU 本地访问辅助宏
 *
 * 按 CPU 路由的本地访问路径在 `BM_CONFIG_CPU_COUNT > 1` 时自动启用；
 * 单 CPU 配置（含本仓默认）下恒为 0。可在包含本头前显式定义
 * `BM_CPU_LOCAL_ENABLE_ROUTE` 覆盖该推导。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-02
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-16       1.0            zeh            新增 CPU 本地访问辅助宏
 * 2026-07-02       1.1            zeh            QD-6：新增 BM_CACHE_LINE_PADDED_UNION，
 *                                                以 union 取代条件 padding 数组消除
 *                                                MSVC C2233（零长数组）
 */
#ifndef BM_CPU_LOCAL_H
#define BM_CPU_LOCAL_H

#include "bm/common/bm_types.h"
#include "hal/bm_hal_cpu.h"

/**
 * @brief 是否启用按 CPU 路由的本地访问路径
 *
 * 由 `BM_CONFIG_CPU_COUNT` 推导：> 1 时启用，否则关闭（单 CPU 配置恒 0）。
 */
#ifndef BM_CPU_LOCAL_ENABLE_ROUTE
#if defined(BM_CONFIG_CPU_COUNT) && (BM_CONFIG_CPU_COUNT > 1u)
#define BM_CPU_LOCAL_ENABLE_ROUTE  1
#else
#define BM_CPU_LOCAL_ENABLE_ROUTE  0
#endif
#endif

#ifndef BM_CACHE_ALIGNAS
#if defined(__GNUC__) || defined(__clang__)
#define BM_CACHE_ALIGNAS(n)  __attribute__((aligned(n)))
#elif defined(_MSC_VER)
#define BM_CACHE_ALIGNAS(n)  __declspec(align(n))
#else
#define BM_CACHE_ALIGNAS(n)
#endif
#endif

/**
 * @brief 生成"向上取整到整 cache-line 倍数"的 union 存储包装类型
 *
 * 按 CPU 分域的存储（bm_event/bm_module/bm_ultra/bm_exec/bm_hrt/bm_sync/
 * bm_ticker/bm_mp_boot 等的 `*_cpu_storage_t`，以及 bm_mp_ipc 的游标类型）
 * 历史上用 `struct { T member; uint8_t padding[cache_line - sizeof(T)%cache_line]; }`
 * 补齐到整 cache-line，防止数组元素间伪共享。但当 `sizeof(T)` 恰为
 * `cache_line` 整数倍时，三元式条件会取 0，产生 `uint8_t padding[0]`——
 * GCC/Clang 靠扩展放行，MSVC 判定"含零长数组的对象数组非法"报 **C2233**。
 *
 * 本宏改用 union：`cache_line_span` 恒 `>= sizeof(T)`（除法向上取整），
 * 故永不产生零长数组；union 总大小 = 向上取整到整 cache-line，
 * 与旧版条件 padding struct 在任意 `sizeof(T)` 下的大小完全一致，
 * 对齐、"无伪共享"语义不变。`member_name_` 保持调用点原有的成员访问方式
 * （如 `.state` / `.value`）不变。
 *
 * @param member_type_  被包裹的状态/数据结构体类型名
 * @param member_name_  包裹后 union 内该成员的访问名（如 state、value）
 * @param cache_line_   cache line 字节数（通常为 BM_CONFIG_CACHE_LINE）
 */
#define BM_CACHE_LINE_PADDED_UNION(member_type_, member_name_, cache_line_) \
    union { \
        member_type_ member_name_; \
        uint8_t cache_line_span[ \
            ((sizeof(member_type_) + (cache_line_) - 1u) / (cache_line_)) * \
            (cache_line_)]; \
    }

/** @brief 当前逻辑 CPU。 */
#ifndef BM_CPU_THIS
#define BM_CPU_THIS()  bm_hal_cpu_id()
#endif

/** @brief 任意 CPU owner 标记。 */
#ifndef BM_CPU_ANY
#define BM_CPU_ANY  0xFFu
#endif

/**
 * @brief 判断当前 CPU 编号是否在配置范围内。
 *
 * @return 非 0 表示有效
 */
static inline int bm_cpu_local_valid(void) {
    return BM_CPU_THIS() < BM_CONFIG_CPU_COUNT;
}

/**
 * @brief 统一 owner 守卫原语：校验当前 CPU 是否有权操作指定 owner 对象。
 *
 * 单核（BM_CPU_LOCAL_ENABLE_ROUTE == 0）：编译期 no-op，恒返回 1，零运行开销。
 * 多核（BM_CPU_LOCAL_ENABLE_ROUTE == 1）：
 *   - @p owner 为 BM_CPU_ANY（0xFFu）时恒真——表示任意核均可操作；
 *   - 否则要求 @p owner < BM_CONFIG_CPU_COUNT 且 BM_CPU_THIS() == @p owner。
 *
 * 各模块（bm_bus / bm_stream / bm_sync 等）在写路径与 owner-only 入口处统一调用
 * 此原语替代各自手写的 `#if BM_CPU_LOCAL_ENABLE_ROUTE` owner 检查，避免重复。
 *
 * @param owner 待校验的 owner_cpu 字段值（来自 bm_stream_t / bm_bus_storage_t 等）
 * @return 非 0 表示当前 CPU 是合法 owner；0 表示越权调用（多核）或单核 no-op（恒 1）
 */
static inline int bm_cpu_is_owner(uint8_t owner) {
#if BM_CPU_LOCAL_ENABLE_ROUTE
    return (owner == (uint8_t)BM_CPU_ANY) ||
           ((owner < (uint8_t)BM_CONFIG_CPU_COUNT) &&
            ((uint8_t)BM_CPU_THIS() == owner));
#else
    (void)owner;
    return 1;
#endif
}

#endif /* BM_CPU_LOCAL_H */
