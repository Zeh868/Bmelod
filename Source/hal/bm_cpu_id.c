/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_cpu_id.c
 * @brief 当前 CPU 编号核心抽象接口的 HAL 实现
 *
 * `bm/common/bm_cpu_local.h` 声明 `bm_cpu_id()`，避免 HAL 反向依赖 core。
 * 本文件提供默认实现：调用平台 HAL 的 `bm_hal_cpu_id()`。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            初始版本：核心抽象接口 HAL 实现
 * 2026-07-28       1.1            zeh            改用 common CPU 本地查询契约
 */
#include "bm/common/bm_cpu_local.h"
#include "hal/bm_hal_cpu.h"

uint32_t bm_cpu_id(void) {
    return bm_hal_cpu_id();
}

int bm_cpu_is_bootstrap(void) {
    return bm_hal_cpu_is_bootstrap();
}
