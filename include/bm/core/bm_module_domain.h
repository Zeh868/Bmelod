/**
 * @file bm_module_domain.h
 * @brief 模块域过滤 API
 *
 * 提供按 RT/SRT/WORKER/COMMON 域初始化、启动、停止、反初始化模块的接口。
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
#ifndef BM_MODULE_DOMAIN_H
#define BM_MODULE_DOMAIN_H

#include "bm/core/bm_module.h"

int bm_module_init_all_for_domain(bm_domain_t domain);
int bm_module_start_all_for_domain(bm_domain_t domain);
int bm_module_stop_all_for_domain(bm_domain_t domain);
int bm_module_deinit_all_for_domain(bm_domain_t domain);

#endif /* BM_MODULE_DOMAIN_H */
