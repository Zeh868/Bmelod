/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_devices.h
 * @brief 跨后端实例出口（应用唯一入口）
 *
 * 机制：后端 pack 以 PUBLIC 宏注入 `BM_HAL_DEVICES_HEADER`
 * （如 `"bm_hal_devices_native.h"`），本头将其展开 include，
 * 聚合该后端导出的全部 `bm_hal_*` 实例声明并提供
 * `bm_<class>_default` 首选实例别名；应用只 include 本头 +
 * 使用 default 别名即与后端解耦。
 *
 * 未注入时为空头（不报错）：纯算法库等无后端场景不受影响。
 * 某外设类后端无实例时不定义对应别名，应用误用为编译期报错
 * （fail-closed）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-08-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-08-01       1.0            zeh            新增（P1 跨后端实例出口）
 *
 */
#ifndef BM_HAL_DEVICES_H
#define BM_HAL_DEVICES_H

#ifdef BM_HAL_DEVICES_HEADER
#include BM_HAL_DEVICES_HEADER   /* 由后端 pack 以 PUBLIC 宏注入 */
#endif

#endif /* BM_HAL_DEVICES_H */
