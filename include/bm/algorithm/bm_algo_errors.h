/**
 * @file bm_algo_errors.h
 * @brief algorithm 层错误码兼容别名
 *
 * 新实现直接使用 bm/common/bm_types.h 中的 BM_OK/BM_ERR_*。本头保留历史
 * BM_ALGO_ERR_* 名称，供既有应用源码平滑迁移；各别名映射到语义对应的公共
 * 错误码，而非统一映射为 -1。
 *
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-02       0.1            zeh            首次归纳并提取（原 Source/algorithm 61 处裸 -1 命名化）
 * 2026-07-28       0.2            zeh            兼容别名改映射到公共 BM_ERR_*
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_ALGO_ERRORS_H
#define BM_ALGO_ERRORS_H

#include "bm/common/bm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 兼容别名：入参非法。 */
#define BM_ALGO_ERR_INVALID   BM_ERR_INVALID

/** @brief 兼容别名：计数或容量溢出。 */
#define BM_ALGO_ERR_OVERFLOW  BM_ERR_OVERFLOW

/** @brief 兼容别名：正常搜索未得到结果。 */
#define BM_ALGO_ERR_NOT_FOUND BM_ERR_NOT_FOUND

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_ERRORS_H */
