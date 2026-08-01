/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_sync_hal.h
 * @brief 同步域平台 HAL 契约
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-08-01       1.0            zeh           补齐 Doxygen 合规元数据
 *
 */
#ifndef BM_SYNC_HAL_H
#define BM_SYNC_HAL_H

#include "bm/hybrid/bm_sync.h"

/**
 * @brief 配置平台同步域硬件
 * @param domain 同步域描述指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；其他为平台错误码
 */
int bm_sync_hal_configure(const bm_sync_domain_t *domain);
/**
 * @brief 武装平台同步域触发源
 * @param domain 同步域描述指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_NOT_INIT 未配置；其他为平台错误码
 */
int bm_sync_hal_arm(const bm_sync_domain_t *domain);
/**
 * @brief 触发已武装的同步域
 * @param domain 同步域描述指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_NOT_INIT 未武装；其他为平台错误码
 */
int bm_sync_hal_trigger(const bm_sync_domain_t *domain);
/**
 * @brief 将同步域硬件置于安全停止状态
 * @param domain 同步域描述指针；NULL 时由平台实现安全处理
 */
void bm_sync_hal_safe_stop(const bm_sync_domain_t *domain);

#endif /* BM_SYNC_HAL_H */
