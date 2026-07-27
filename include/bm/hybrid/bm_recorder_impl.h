/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_recorder_impl.h
 * @brief bm_recorder 静态存储分配内部头
 *
 * 本头文件提供 `BM_RECORDER_DEFINE` 宏，用于在编译期为录波环实例静态分配
 * 帧缓冲区。该宏涉及内部存储布局，不属于公开 API 兼容性承诺范围，
 * 应用方如需使用请 include 本内部头。
 *
 * @note 公开头 `bm_recorder.h` 仅保留类型定义与函数声明。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       0.1            zeh            从 bm_recorder.h 迁出静态分配宏
 *
 */
#ifndef BM_RECORDER_IMPL_H
#define BM_RECORDER_IMPL_H

#include "bm/hybrid/bm_recorder.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 静态定义录波环实例及 backing buffer（编译期校验 depth 为 2 的幂）
 *
 * depth 必须满足 depth>=2 且为 2 的幂，否则触发负数组下标编译错误。
 * 示例：BM_RECORDER_DEFINE(foc_rec, foc_bb_frame_t, 1024);
 *
 * @param name  实例标识符
 * @param type  单帧类型
 * @param depth 帧深（2 的幂，>=2）
 */
#define BM_RECORDER_DEFINE(name, type, depth)                                  \
    typedef char _bm_rec_chk_##name[((depth) >= 2 &&                           \
        (((depth) & ((depth) - 1u)) == 0u)) ? 1 : -1];                         \
    static uint8_t _bm_rec_buf_##name[(depth) * sizeof(type)];                 \
    static bm_recorder_t name = { _bm_rec_buf_##name, sizeof(type),            \
        (depth), (depth) - 1u, 0, 0, 0u, 0u }

#ifdef __cplusplus
}
#endif

#endif /* BM_RECORDER_IMPL_H */
