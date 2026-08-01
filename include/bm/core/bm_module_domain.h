/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_module_domain.h
 * @brief 模块域过滤 API
 *
 * 提供按 RT/SRT/WORKER/COMMON 域初始化、启动、停止、反初始化模块的接口。
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-08-01       1.0            zeh           补齐 Doxygen 合规元数据
 *
 */
#ifndef BM_MODULE_DOMAIN_H
#define BM_MODULE_DOMAIN_H

#include "bm/core/bm_module.h"

/**
 * @brief 初始化匹配指定 domain 的模块（不启动）
 *
 * 仅加载 domain 与 BM_DOMAIN_COMMON 模块到内部工作表并依次调用其 init。
 *
 * @param domain 目标域
 * @return BM_OK 成功；负值表示失败
 */
int bm_module_init_all_for_domain(bm_domain_t domain);

/**
 * @brief 启动匹配指定 domain 的已初始化模块
 *
 * @param domain 目标域
 * @return BM_OK 全部成功；负值为首个失败模块的错误码
 */
int bm_module_start_all_for_domain(bm_domain_t domain);

/**
 * @brief 停止匹配指定 domain 的已启动模块
 *
 * @param domain 目标域
 * @return BM_OK 全部成功；负值为首个失败模块的错误码
 */
int bm_module_stop_all_for_domain(bm_domain_t domain);

/**
 * @brief 反初始化匹配指定 domain 的模块
 *
 * 若反初始化后仍有其他域模块处于活动状态，则保持 READY；否则回到 UNINITIALIZED。
 *
 * @param domain 目标域
 * @return BM_OK 全部成功；负值为首个失败模块的错误码
 */
int bm_module_deinit_all_for_domain(bm_domain_t domain);

#endif /* BM_MODULE_DOMAIN_H */
