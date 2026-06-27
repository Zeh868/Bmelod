/**
 * @file bm_profile_epoch.h
 * @brief profile 代际查询钩子（解耦 bm_exec 与 bm_mp）
 *
 * relay / stream 在 profile build 后通过注册回调读取当前代际，
 * 避免 bm_exec 与 bm_mp 循环链接。
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
#ifndef BM_PROFILE_EPOCH_H
#define BM_PROFILE_EPOCH_H

#include <stdint.h>

typedef uint32_t (*bm_profile_epoch_query_fn_t)(void);

/**
 * @brief 注册 profile 代际查询函数（`bm_mp_profile_build` 成功后调用）
 *
 * @param fn 查询回调；NULL 表示清除
 */
void bm_profile_epoch_register(bm_profile_epoch_query_fn_t fn);

/**
 * @brief 读取当前 profile 代际
 *
 * @return 已注册且 build 成功时 >= 1；未注册时 0
 */
uint32_t bm_profile_epoch_current(void);

#endif /* BM_PROFILE_EPOCH_H */
