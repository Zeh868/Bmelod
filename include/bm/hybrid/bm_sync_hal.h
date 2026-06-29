/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_sync_hal.h
 * @brief 同步域平台 HAL 契约
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 *
 */
#ifndef BM_SYNC_HAL_H
#define BM_SYNC_HAL_H

#include "bm/hybrid/bm_sync.h"

int bm_sync_hal_configure(const bm_sync_domain_t *domain);
int bm_sync_hal_arm(const bm_sync_domain_t *domain);
int bm_sync_hal_trigger(const bm_sync_domain_t *domain);
void bm_sync_hal_safe_stop(const bm_sync_domain_t *domain);

#endif /* BM_SYNC_HAL_H */
