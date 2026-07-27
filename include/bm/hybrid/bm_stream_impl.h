/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_stream_impl.h
 * @brief bm_stream 静态存储分配内部头
 *
 * 本头文件提供 `BM_STREAM_PAYLOADS`、`BM_STREAM_BLOCKS`、`BM_STREAM_INSTANCE`
 * 宏，用于在编译期为 stream 实例静态分配 payload、block 描述符及 stream 控制块。
 * 该宏涉及内部存储布局，不属于公开 API 兼容性承诺范围，应用方如需使用请
 * include 本内部头。
 *
 * @note 公开头 `bm_stream.h` 仅保留类型定义与函数声明。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       0.1            zeh            从 bm_stream.h 迁出静态分配宏
 *
 */
#ifndef BM_STREAM_IMPL_H
#define BM_STREAM_IMPL_H

#include "bm/hybrid/bm_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BM_STREAM_PAYLOADS(name, type, depth) \
    static type _bm_stream_payload_##name[(depth)]

#define BM_STREAM_BLOCKS(name, depth) \
    static bm_block_t _bm_stream_blocks_##name[(depth)]

#define BM_STREAM_INSTANCE(name, depth) \
    static bm_stream_t name = { \
        .blocks = _bm_stream_blocks_##name, \
        .block_count = (depth), \
        .block_capacity = (depth), \
        .policy = BM_STREAM_POLICY_DROP_NEWEST, \
        .owner_cpu = 0u \
    }

#ifdef __cplusplus
}
#endif

#endif /* BM_STREAM_IMPL_H */
