/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_cpu_mp_native.h
 * @brief native_sim 多核 CPU HAL 测试辅助接口
 * @maturity E1
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-08-01       1.0            zeh            补全中文 Doxygen 合规注释
 */
#ifndef BM_HAL_CPU_MP_NATIVE_H
#define BM_HAL_CPU_MP_NATIVE_H

#include <stdint.h>

/**
 * @brief 设置当前宿主线程的逻辑 CPU 编号。
 * @param cpu 逻辑 CPU 索引。
 * @return 成功返回 BM_OK；CPU 索引或 TLS 状态无效时返回 BM_ERR_INVALID。
 */
int bm_hal_cpu_native_set_id(uint32_t cpu);

#endif /* BM_HAL_CPU_MP_NATIVE_H */
