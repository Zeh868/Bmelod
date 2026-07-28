/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_cpu_local.h
 * @brief CPU 本地查询的零上层依赖契约
 *
 * 声明由 HAL 实现的当前 CPU 查询包装，使 HAL、core 及上层模块共享同一
 * 下层可依赖接口，避免 HAL 反向包含 core 头文件。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            下沉 CPU 本地查询公共契约
 */
#ifndef BM_COMMON_CPU_LOCAL_H
#define BM_COMMON_CPU_LOCAL_H

#include "bm/common/bm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 查询当前逻辑 CPU 编号。
 *
 * 由 HAL 映射至平台 CPU ID 原语；单核配置下通常返回 0。
 *
 * @return 当前 CPU 编号。
 */
uint32_t bm_cpu_id(void);

/**
 * @brief 判断当前 CPU 是否为 bootstrap 核。
 *
 * 由 HAL 映射至平台启动核判定；单核配置下恒为真。
 *
 * @return 非 0 表示当前 CPU 是 bootstrap 核。
 */
int bm_cpu_is_bootstrap(void);

#ifdef __cplusplus
}
#endif

#endif /* BM_COMMON_CPU_LOCAL_H */
