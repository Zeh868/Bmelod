/**
 * @file bm_mp_types.h
 * SPDX-License-Identifier: LicenseRef-Bmeflod-Proprietary
 * @brief MP 闭源扩展公共 API · 需 bm_mp
 *
 * 定义 `BM_MP_*` 剖面枚举、`BM_CONFIG_CPU_COUNT` 与 `BM_MP_MULTICORE` 推导宏。
 * 单核产品保持 `CPU_COUNT==1`，与历史 `BM_MP_SINGLE` 行为等价。
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
#ifndef BM_MP_TYPES_H
#define BM_MP_TYPES_H

#include "bm_config.h"
/*
 * 经由唯一来源 bm_cpu_local.h 提供 BM_CACHE_ALIGNAS / BM_CPU_THIS / BM_CPU_ANY，
 * 并确保 BM_CPU_LOCAL_ENABLE_ROUTE 在下方 BM_MP_MULTICORE 推导前已定义。
 */
#include "bm/core/bm_cpu_local.h"

/*
 * 跨核共享内存放置（确定性流式跨核一致性约定）。
 *
 * relay 缓冲与 IPC 矩阵以"显式 memcpy + release/acquire fence"协议跨核通信，
 * 但**不**对这些缓冲做 CPU D-cache 维护。因此在带 D-cache 的真机多核上，它们
 * 必须落在 non-cacheable 或硬件 cache-coherent 区，否则跨核读取会见到陈旧
 * cache 行，造成静默数据损坏（软剖面同样适用，并非仅 hard-RT）。
 *
 * BM_MP_SHARED_SECTION：可选链接 section 属性。带 cache 的多核 Port 应将其
 *   定义为落在 non-cacheable / coherent 区的 section，例如：
 *     #define BM_MP_SHARED_SECTION __attribute__((section(".noncacheable")))
 *   单核 / native_sim（cache 维护为 no-op）留空即可。
 *
 * BM_MP_SHARED_PLACEMENT_VERIFIED：Port 的显式承诺——所有跨核共享内存
 *   （IPC 矩阵 + relay 缓冲）已落在 non-cacheable / coherent 区（经
 *   BM_MP_SHARED_SECTION 链接 section、或 attach()/显式 base 指向的区域）。
 *   带 D-cache 的多核（BM_HAL_CACHE_IS_NOOP==0）下必须置 1；否则相关 TU
 *   会编译期 #error，避免靠"自觉"导致的静默跨核损坏。
 */
#ifndef BM_MP_SHARED_SECTION
#define BM_MP_SHARED_SECTION
#endif

#ifndef BM_MP_SHARED_PLACEMENT_VERIFIED
#define BM_MP_SHARED_PLACEMENT_VERIFIED  0
#endif

/** 单 MCU 单运行时（BM_CONFIG_CPU_COUNT==1） */
#define BM_MP_SINGLE   0
/** 对称 AMP 多镜像（每镜像须 BM_CONFIG_CPU_COUNT==1） */
#define BM_MP_AMP      1
/** 已废弃：请使用 BM_MP_PERCPU */
#define BM_MP_RTD      2
/** 单 ELF、每核一套 HSRT/SHRT/SRT；硬件可 SMP，软件按 AMP 语义 */
#define BM_MP_PERCPU   3

#ifndef BM_CONFIG_TOPOLOGY
#define BM_CONFIG_TOPOLOGY  BM_MP_PERCPU
#endif

#ifndef BM_CONFIG_MP_TOPOLOGY
#define BM_CONFIG_MP_TOPOLOGY  BM_CONFIG_TOPOLOGY
#endif

#if defined(BM_CONFIG_MP_TOPOLOGY) && !defined(BM_CONFIG_TOPOLOGY)
#define BM_CONFIG_TOPOLOGY  BM_CONFIG_MP_TOPOLOGY
#endif

#ifndef BM_CONFIG_CPU_COUNT
#define BM_CONFIG_CPU_COUNT  1u
#endif

#ifndef BM_CONFIG_CACHE_LINE
#define BM_CONFIG_CACHE_LINE  64u
#endif

/** 分区器自动分配 owner；不得用于 HAL 绑核对象（与 bm_cpu_local.h 同值） */
#ifndef BM_CPU_ANY
#define BM_CPU_ANY  0xFFu
#endif

#if (BM_CONFIG_CPU_COUNT < 1u) || (BM_CONFIG_CPU_COUNT > 8u)
#error "BM_CONFIG_CPU_COUNT 须处于 [1, 8]"
#endif

#if (BM_CONFIG_CACHE_LINE < 4u) || \
    ((BM_CONFIG_CACHE_LINE & (BM_CONFIG_CACHE_LINE - 1u)) != 0u)
#error "BM_CONFIG_CACHE_LINE must be a power of two and at least 4"
#endif

#if BM_CONFIG_CPU_COUNT > 1u
#if BM_CONFIG_TOPOLOGY != BM_MP_PERCPU
#error "multi-core single ELF requires BM_MP_PERCPU (AMP uses one ELF per core with CPU_COUNT==1)"
#endif

#ifndef BM_MP_MULTICORE
#define BM_MP_MULTICORE  BM_CPU_LOCAL_ENABLE_ROUTE
#endif
#endif

#if BM_CONFIG_TOPOLOGY == BM_MP_RTD
#error "BM_MP_RTD removed; use BM_MP_PERCPU"
#endif

#if BM_CONFIG_TOPOLOGY == BM_MP_AMP && BM_CONFIG_CPU_COUNT > 1u
#error "BM_MP_AMP requires BM_CONFIG_CPU_COUNT==1 per firmware image"
#endif

#endif /* BM_MP_TYPES_H */
