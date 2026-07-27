/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_log_output.c
 * @brief 默认日志输出提供者（转发 Console LOG 通道）
 *
 * 独立编译单元，便于在不支持弱符号的工具链上由应用提供强符号 `bm_log_output`。
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-06-15       1.1            zeh            hard RT 禁止 stdio 输出
 * 2026-06-19       1.2            zeh            默认经 bm_hal_console LOG 通道输出
 *
 */
#include "bm_config.h"
#include "bm_log.h"
#include "hal/bm_hal_console.h"

/**
 * @brief 默认日志输出钩子
 *
 * 将格式化日志写入 Console LOG 通道；应用可链接强符号覆盖以自定义 sink。
 *
 * @param buf 日志缓冲区指针
 * @param len 日志长度
 */
void bm_log_output(const char *buf, size_t len) {
    if (!buf || len == 0u) {
        return;
    }
    (void)bm_hal_console_write(BM_CONSOLE_LOG, (const uint8_t *)buf, len);
}
