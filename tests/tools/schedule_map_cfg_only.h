/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file schedule_map_cfg_only.h
 * @brief schedule-map config 单源展开注入测试用 config 头
 *
 * @details 仅测试用：模拟应用工程在 bm_config_app.h 里的 config 单源声明——
 * 不经 bm_add_schedule_map() 的 REF_CLK_HZ/OPERATING_POINTS/INTERFERENCE_SRC
 * 显式 CMake 参数，而是把频率与干扰源都交给 config 宏单源展开（BM_CONFIG_CPU_FREQ_HZ /
 * BM_CONFIG_CPU_DVFS_POINTS_HZ / BM_CONFIG_SM_INTERFERENCE_SRC），验证
 * cmake/bm_schedule_map.cmake 生成的 `#ifdef` 回退链在三个宏都生效。
 * 经 -include（gcc）/ /FI（MSVC）强制预包含到生成的 reg TU。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-04
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-04       1.0            zeh            Task 4：config 单源展开注入测试头初版
 *
 */
#define BM_CONFIG_CPU_FREQ_HZ         240000000u
#define BM_CONFIG_CPU_DVFS_POINTS_HZ  { 240000000u, 120000000u }
#define BM_CONFIG_SM_INTERFERENCE_SRC { { { "cfg_hw", 500u, 5u, 0u }, 0u } }
