/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_bus_impl.h
 * @brief bm_bus 静态存储分配内部头
 *
 * 本头文件提供 `BM_BUS_DEFINE` 宏，用于在编译期为 bus 存储对象及伴生缓冲区
 * 静态分配存储。该宏涉及内部存储布局（数据缓冲区、读者游标数组、存储控制块），
 * 不属于公开 API 兼容性承诺范围，应用方如需使用请 include 本内部头。
 *
 * @note 公开头 `bm_bus.h` 仅保留类型定义与函数声明。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       0.1            zeh            从 bm_bus.h 迁出静态分配宏
 * 2026-08-01       0.1            zeh           补齐 Doxygen 合规元数据
 *
 */
#ifndef BM_BUS_IMPL_H
#define BM_BUS_IMPL_H

#include "bm/core/bm_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 编译期静态定义 bus 存储对象及伴生缓冲区
 *
 * 展开后生成：
 *   - 编译期容量断言（capacity >= 2）
 *   - 编译期 2 的幂断言（QUEUE/SIGNAL 专属，防 2^32 游标回绕静默损坏）
 *   - 静态数据缓冲区 `_bm_bus_data_<name>`
 *   - 静态读者游标数组 `_bm_bus_readers_<name>`
 *   - 静态存储控制块 `<name>_storage`（类型 bm_bus_storage_t）
 *
 * @note 编译期断言两条：(1) capacity>=2（所有 mode 通用下界）；
 *       (2) QUEUE/SIGNAL 的 capacity 须为 2 的幂——自由递增游标取模 cap，
 *       非 2 的幂在 2^32 回绕处取模不连续会致一次静默错读，强制 2 的幂使回绕无缝
 *       （LATEST/BLOCK 不用 write_cur，豁免）。LATEST 三缓冲的 capacity>=3 约束
 *       （多核防撕裂，spec §7）由运行期 bus_storage_valid / bm_bus_open 校验拦截。
 *
 * @param name          bus 实例名（不带引号，展开为 name##_storage）
 * @param type          元素类型
 * @param capacity      环槽总数（须 >= 2）
 * @param max_consumers 最大读者数
 * @param mode          bm_bus_mode_t 枚举值
 */
#define BM_BUS_DEFINE(name, type, cap_, maxcons_, mode_)                   \
    typedef char _bm_bus_cap_check_##name[((cap_) >= 2u) ? 1 : -1];       \
    typedef char _bm_bus_pow2_check_##name[                                \
        ((mode_) == BM_BUS_LATEST || (mode_) == BM_BUS_BLOCK ||            \
         (mode_) == BM_BUS_IPC   ||                                        \
         (((cap_) & ((cap_) - 1u)) == 0u)) ? 1 : -1];                     \
    static uint8_t _bm_bus_data_##name[(cap_) * sizeof(type)];             \
    static bm_bus_reader_slot_t _bm_bus_readers_##name[(maxcons_)];        \
    static bm_bus_storage_t name##_storage = {                             \
        .write_cur        = BM_ATOMIC_IPC_U32_INIT(0u),                    \
        .latest_published = BM_ATOMIC_IPC_U32_INIT(0u),                    \
        .latest_reading   = BM_ATOMIC_IPC_U32_INIT(BM_BUS_LATEST_NONE),    \
        .latest_writing   = BM_ATOMIC_IPC_U32_INIT(0u),                    \
        .latest_seq       = BM_ATOMIC_IPC_U32_INIT(0u),                    \
        .data_buf         = _bm_bus_data_##name,                           \
        .elem_size        = sizeof(type),                                   \
        .capacity         = (uint32_t)(cap_),                              \
        .max_consumers    = (uint32_t)(maxcons_),                          \
        .mode             = (mode_),                                        \
        .owner_cpu        = 0u,                                             \
        .frozen           = 0u,                                             \
        .write_in_progress= 0u,                                             \
        .reader_count     = 0u,                                             \
        .readers          = _bm_bus_readers_##name,                        \
    }

#ifdef __cplusplus
}
#endif

#endif /* BM_BUS_IMPL_H */
