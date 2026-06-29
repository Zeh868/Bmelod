/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_console_stdio.c
 * @brief Console STDIO 后端（stdout/stdin）
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

#include <stdio.h>

/**
 * @brief 初始化 STDIO 后端
 */
int bm_console_stdio_init(void) {
    return BM_OK;
}

/**
 * @brief 经 stdout 写出
 */
int bm_console_stdio_write(const uint8_t *data, size_t len) {
    if (!data || len == 0u) {
        return BM_ERR_INVALID;
    }
    if (fwrite(data, 1, len, stdout) != len) {
        return BM_ERR_BUSY;
    }
    fflush(stdout);
    return BM_OK;
}

/**
 * @brief 经 stdin 非阻塞读（无数据时返回 0）
 */
size_t bm_console_stdio_read(uint8_t *data, size_t max_len) {
    size_t n = 0u;

    if (!data || max_len == 0u) {
        return 0u;
    }
    while (n < max_len) {
        int c = getchar();
        if (c == EOF) {
            break;
        }
        data[n++] = (uint8_t)c;
    }
    return n;
}
