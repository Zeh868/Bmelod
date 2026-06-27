/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_console_stub.c
 * @brief Console 空操作后端
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            初版
 *
 */
#include "bm_types.h"
#include <stddef.h>

/**
 * @brief 初始化 stub 后端
 */
int bm_console_stub_init(void) {
    return BM_OK;
}

/**
 * @brief stub 写：丢弃
 */
int bm_console_stub_write(const uint8_t *data, size_t len) {
    (void)data;
    (void)len;
    return BM_OK;
}

/**
 * @brief stub 读：无数据
 */
size_t bm_console_stub_read(uint8_t *data, size_t max_len) {
    (void)data;
    (void)max_len;
    return 0u;
}
