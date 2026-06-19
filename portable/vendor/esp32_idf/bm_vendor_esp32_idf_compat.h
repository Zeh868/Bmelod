/**
 * @file bm_vendor_esp32_idf_compat.h
 * @brief ESP32-IDF vendor 兼容宏与局部退化定义
 *
 * 仅供 `portable/vendor/esp32_idf` 目录内部使用，不向公共头文件扩散。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            新增 vendor 私有 I/O 错误兼容宏
 *
 */
#ifndef BM_VENDOR_ESP32_IDF_COMPAT_H
#define BM_VENDOR_ESP32_IDF_COMPAT_H

#include "bm_types.h"

#ifndef BM_ERR_IO
/**
 * @brief 平台 I/O 错误兼容别名。
 *
 * 仅在 vendor 内部缺少显式 I/O 错误码时，退化映射到“不支持”。
 */
#define BM_ERR_IO BM_ERR_NOT_SUPPORTED
#endif

#endif /* BM_VENDOR_ESP32_IDF_COMPAT_H */
