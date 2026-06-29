/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_cpu_mp_native.h
 * @brief native_sim 多核 CPU HAL 测试辅助接口
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 *
 */
#ifndef BM_HAL_CPU_MP_NATIVE_H
#define BM_HAL_CPU_MP_NATIVE_H

#include <stdint.h>

/**
 * @brief Set the current native thread's logical CPU id.
 *
 * This helper is intended for native_sim tests only.
 *
 * @return BM_OK on success; BM_ERR_INVALID if cpu is out of range.
 */
int bm_hal_cpu_native_set_id(uint32_t cpu);

#endif /* BM_HAL_CPU_MP_NATIVE_H */
