/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_shell_port.c
 * @brief shell CLI 通道的 HAL 端口实现
 *
 * 将 `bm_shell` 的字符读写转发到 Console CLI 通道，避免 core 层直接
 * include `hal/bm_hal_console.h`。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            初始版本：shell CLI 端口抽象
 * 2026-08-01       1.0            zeh           补全 Doxygen 合规注释
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "hal/bm_hal_console.h"

#include <stddef.h>
#include <stdint.h>

int bm_shell_port_write(const uint8_t *data, size_t len) {
    return bm_hal_console_write(BM_CONSOLE_CLI, data, len);
}

size_t bm_shell_port_read(uint8_t *data, size_t max_len) {
    return bm_hal_console_read(BM_CONSOLE_CLI, data, max_len);
}
