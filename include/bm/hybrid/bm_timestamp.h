/**
 * @file bm_timestamp.h
 * @brief 兼容转发头 —— bm_timestamp_t 已下沉至 bm/common/bm_timestamp.h
 *
 * 路线图 #9 时间基统一 1b：bm_timestamp_t 定义已移至 common 层，
 * 本文件保留为兼容转发头，现有 27 个引用文件无需任何改动即可继续编译。
 *
 * 如需新引用，建议直接包含 "bm/common/bm_timestamp.h"。
 *
 * @author zeh (china_qzh@163.com)
 * @version 2.0
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-12       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            clock_id 辅助
 * 2026-06-26       2.0            zeh            改为转发头（定义已下沉至 common，#9-1b）
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_TIMESTAMP_H
#define BM_TIMESTAMP_H

#include "bm/common/bm_timestamp.h"

#endif /* BM_TIMESTAMP_H */
