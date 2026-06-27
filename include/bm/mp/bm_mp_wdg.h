/**
 * @file bm_mp_wdg.h
 * SPDX-License-Identifier: LicenseRef-Bmeflod-Proprietary
 * @brief MP 闭源扩展公共 API · 需 bm_mp
 *
 * 每核周期性递增 `cpu_hb_seq`；任意核调用 `bm_mp_wdg_bootstrap_check()` 可检
 * 查所有其他核心跳。超时触发全局 safe-stop 钩子（仅触发一次，`bm_mp_wdg_reset`
 * 复位）。bootstrap 核可额外检查从核尚未启动的情况（s_seen 保护）。
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            每核互相监督 + safe-stop 防重入
 *
 */
#ifndef BM_MP_WDG_H
#define BM_MP_WDG_H

#include "bm/common/bm_types.h"

#ifndef BM_CONFIG_WDG_HB_TIMEOUT_MS
#define BM_CONFIG_WDG_HB_TIMEOUT_MS  500u
#endif

#ifndef BM_CONFIG_MP_WDG_HB_TIMEOUT_MS
#define BM_CONFIG_MP_WDG_HB_TIMEOUT_MS  BM_CONFIG_WDG_HB_TIMEOUT_MS
#endif

#if defined(BM_CONFIG_MP_WDG_HB_TIMEOUT_MS) && \
    !defined(BM_CONFIG_WDG_HB_TIMEOUT_MS)
#define BM_CONFIG_WDG_HB_TIMEOUT_MS  BM_CONFIG_MP_WDG_HB_TIMEOUT_MS
#endif

/**
 * @brief 本核递增 heartbeat 计数（主循环每圈调用）
 */
void bm_mp_wdg_feed_this_cpu(void);

/**
 * @brief Bootstrap 检查全部已启动核的 heartbeat
 *
 * @return BM_OK 全部新鲜；BM_ERR_TIMEOUT 某核超时
 */
int bm_mp_wdg_bootstrap_check(void);

/**
 * @brief 注册全局 safe-stop 钩子（超时或监督失败时调用）
 */
typedef void (*bm_mp_wdg_safe_stop_fn_t)(void);
void bm_mp_wdg_set_safe_stop_hook(bm_mp_wdg_safe_stop_fn_t hook);
void bm_mp_wdg_reset(void);

#endif /* BM_MP_WDG_H */
