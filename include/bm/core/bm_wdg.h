/**
 * @file bm_wdg.h
 * @brief 软件看门狗：由应用 main 主循环统一喂硬件狗
 *
 * 应用侧约定：bm_module 不参与喂狗；主循环每圈调用 bm_wdg_feed() 即可。
 * 未注册软件模块时直接 bm_hal_wdg_feed()。
 * bm_wdg_register / bm_wdg_feed_module 仅供多心跳 AND 聚合等高级场景与单元测试。
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-10       1.1            zeh            对齐超时语义与返回值说明
 *
 */
#ifndef BM_WDG_H
#define BM_WDG_H

#include "bm/common/bm_types.h"

#ifndef BM_CONFIG_MAX_WDG_MODULES
#define BM_CONFIG_MAX_WDG_MODULES 4
#endif

#ifndef BM_CONFIG_WDG_MODULE_TIMEOUT_MS
#define BM_CONFIG_WDG_MODULE_TIMEOUT_MS 1000
#endif

#ifndef BM_CONFIG_WDG_MAX_NAME_LEN
#define BM_CONFIG_WDG_MAX_NAME_LEN 32
#endif

#if BM_CONFIG_WDG_MAX_NAME_LEN < 2
#error "BM_CONFIG_WDG_MAX_NAME_LEN must be at least 2"
#endif

/**
 * @brief 注册一个需喂狗的逻辑模块
 *
 * @param name 模块名称（非 NULL，注册时复制）
 * @return BM_OK 成功；BM_ERR_NO_MEM 注册表已满；BM_ERR_INVALID 参数无效；
 *         BM_ERR_ALREADY 同名模块已注册
 */
int bm_wdg_register(const char *name);

/**
 * @brief 检查所有模块已按时喂狗后喂硬件看门狗
 */
void bm_wdg_feed(void);

/**
 * @brief 注册硬件喂狗前的放行检查钩子（NULL 清除）。
 *
 * bm_wdg_feed 在执行硬件喂狗前调用；钩子返回非 0（非 BM_OK）则本轮
 * 抑制硬件喂狗。默认未设时直接放行。用于上层附加聚合健康判定。
 */
void bm_wdg_set_gate_hook(int (*hook)(void));

/**
 * @brief 按名称记录模块喂狗时间戳
 *
 * @param name 模块名称（非 NULL）
 */
void bm_wdg_feed_module(const char *name);

/**
 * @brief 清空软件看门狗注册表（仅用于测试或停机）
 */
void bm_wdg_reset(void);

#endif /* BM_WDG_H */
