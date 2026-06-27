/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_atomic.h
 * @brief 原子变量读写操作
 *
 * 单核：关中断实现（低开销）。
 *
 * 统一提供 32 位无符号整数的读写、自增接口。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            统一原子实现
 *
 */
#ifndef BM_ATOMIC_H
#define BM_ATOMIC_H

#include "bm/common/bm_types.h"

/**
 * @brief 原子读取 32 位无符号整数值
 *
 * @param value 原子变量指针
 * @return 当前存储的值
 */
uint32_t bm_atomic_load(bm_atomic_t *value);

/**
 * @brief 原子写入 32 位无符号整数值
 *
 * @param value 原子变量指针
 * @param new_value 待写入的新值
 */
void bm_atomic_store(bm_atomic_t *value, uint32_t new_value);

/**
 * @brief 原子自增并返回递增后的值（UINT32_MAX 处饱和）
 *
 * @param value 原子变量指针
 * @return 自增后的值；已达 UINT32_MAX 时保持不变
 */
uint32_t bm_atomic_inc(bm_atomic_t *value);

#endif /* BM_ATOMIC_H */
