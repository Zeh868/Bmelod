/**
 * @file bm_config_app.h
 * @brief ATK-DMG474 冒烟工程 force-include 配置
 *
 * 由 CMake `-include` 预包含；勿定义 BM_CONFIG_H，以便仍加载框架默认项。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-30
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-30       1.0            zeh            新增 ATK-DMG474 RTT 冒烟配置
 */

/* 最小剖面：不要模块/看门狗/Shell，日志走 RTT
 * MODULE/WDG 由 CMake BM_ENABLE_*=OFF 注入，勿在此重复 #define（避免与 -D 冲突）。 */
#define BM_CONFIG_ENABLE_ULTRA               0
#define BM_CONFIG_ENABLE_SHELL               0
#define BM_CONFIG_ENABLE_HRT                 0
#define BM_CONFIG_ENABLE_TICKER              0
#define BM_CONFIG_ENABLE_EXEC                0
#define BM_CONFIG_ENABLE_SYNC                0
#define BM_CONFIG_ENABLE_STREAM              0
#define BM_CONFIG_ENABLE_PIPELINE            0
#define BM_CONFIG_ENABLE_ALGORITHM           0
#define BM_CONFIG_ENABLE_TT_SCHED            0
#define BM_CONFIG_ENABLE_WCET_MON            0

#define BM_CONFIG_ENABLE_LOG                 1
#define BM_CONFIG_LOG_LEVEL                  2
#define BM_CONFIG_LOG_USE_STDIO              0
#define BM_CONFIG_LOG_RING                   0

/* 0=NONE,1=STDIO,2=UART,3=RTT（数值须在 bm_config.h 枚举宏定义之前可用） */
#define BM_CONFIG_CONSOLE_LOG_BACKEND        3
#define BM_CONFIG_CONSOLE_CLI_BACKEND        0
