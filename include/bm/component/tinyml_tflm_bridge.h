/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file tinyml_tflm_bridge.h
 * @brief TinyML 与 TFLite Micro 的无库依赖桥接接口。
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.6
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            TFLM 桥接契约与 arena 导出辅助
 * 2026-06-17       0.5            zeh            对接 TFLM runtime
 * 2026-07-28       0.6            zeh            桥接契约下沉至 bm/common，移除 adapter 组件依赖
 */
#ifndef BM_TINYML_TFLM_BRIDGE_H
#define BM_TINYML_TFLM_BRIDGE_H

#include "bm/common/bm_tinyml_contract.h"

/**
 * @brief 本头仅重导出通用 bridge 契约。
 *
 * `bm_tflm_bridge_lookup_op()` 返回 TFLM BuiltinOperator ID 载荷；未找到时
 * 返回 -1 作为载荷哨兵，而非框架状态码。TFLM 的实际库头和解释器仍由用户工程链接。
 */

#endif /* BM_TINYML_TFLM_BRIDGE_H */
