/**
 * @file bm_fault.h
 * @brief 系统级统一故障码与严重度定义（跨组件健康上报的公共词汇）
 *
 * component 层各诊断组件（sensor_quality / spectral_diagnostics /
 * fault_derating 等）内部仍使用各自的位掩码（如 BM_ALGO_FAULT_*），
 * 本头文件定义的是“系统级”故障码空间：应用在上报给 health_monitor
 * 等聚合组件时，把组件私有的 fault_flags 映射为这里的 bm_fault_code_t。
 *
 * 码空间按域分段（每域 256 个码位），各域当前只定义少量通用码，
 * 其余码位保留，由对应域组件后续填充；新码只允许追加，不允许改值。
 *
 * 本头文件为纯定义，无任何依赖、零运行时成本。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       0.1            zeh            初始版本：严重度枚举 + 按域分段的故障码
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_FAULT_H
#define BM_FAULT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 故障严重度（数值越大越严重，聚合时可直接比较取最大）
 */
typedef enum {
    BM_FAULT_SEVERITY_NONE     = 0, /**< 无故障 */
    BM_FAULT_SEVERITY_INFO     = 1, /**< 提示：不影响功能 */
    BM_FAULT_SEVERITY_WARNING  = 2, /**< 警告：性能降级或需关注 */
    BM_FAULT_SEVERITY_ERROR    = 3, /**< 错误：功能受损，需响应（如降额） */
    BM_FAULT_SEVERITY_CRITICAL = 4  /**< 严重：安全相关，需立即处置 */
} bm_fault_severity_t;

/**
 * @brief 系统级故障码（uint16；0 表示无故障，高 8 位为域编号）
 */
typedef uint16_t bm_fault_code_t;

/** @brief 无故障（上报此码表示清除该源的活动故障） */
#define BM_FAULT_NONE ((bm_fault_code_t)0x0000u)

/* ---------- 域基址（每域 256 码位，0x00 保留不用） ---------- */
#define BM_FAULT_DOMAIN_GENERIC_BASE  ((bm_fault_code_t)0x0000u) /**< 通用域 */
#define BM_FAULT_DOMAIN_SENSOR_BASE   ((bm_fault_code_t)0x0100u) /**< 传感器域 */
#define BM_FAULT_DOMAIN_MOTION_BASE   ((bm_fault_code_t)0x0200u) /**< 运动/电机域（保留） */
#define BM_FAULT_DOMAIN_POWER_BASE    ((bm_fault_code_t)0x0300u) /**< 电源/BMS 域（保留） */
#define BM_FAULT_DOMAIN_TRANSPORT_BASE ((bm_fault_code_t)0x0400u) /**< 传输/通信域（保留） */

/* ---------- 通用域 ---------- */
#define BM_FAULT_GENERIC_UNKNOWN \
    ((bm_fault_code_t)(BM_FAULT_DOMAIN_GENERIC_BASE + 0x01u)) /**< 未分类故障 */

/* ---------- 传感器域（与 BM_ALGO_FAULT_* 语义一一对应，便于应用映射） ---------- */
#define BM_FAULT_SENSOR_UNDER_RANGE \
    ((bm_fault_code_t)(BM_FAULT_DOMAIN_SENSOR_BASE + 0x01u)) /**< 采样低于量程下限 */
#define BM_FAULT_SENSOR_OVER_RANGE \
    ((bm_fault_code_t)(BM_FAULT_DOMAIN_SENSOR_BASE + 0x02u)) /**< 采样高于量程上限 */
#define BM_FAULT_SENSOR_RATE \
    ((bm_fault_code_t)(BM_FAULT_DOMAIN_SENSOR_BASE + 0x03u)) /**< 变化率超限 */
#define BM_FAULT_SENSOR_FROZEN \
    ((bm_fault_code_t)(BM_FAULT_DOMAIN_SENSOR_BASE + 0x04u)) /**< 冻结值（读数长期不变） */
#define BM_FAULT_SENSOR_NAN \
    ((bm_fault_code_t)(BM_FAULT_DOMAIN_SENSOR_BASE + 0x05u)) /**< 非有限值（NaN/Inf） */
#define BM_FAULT_SENSOR_REDUNDANT_MISMATCH \
    ((bm_fault_code_t)(BM_FAULT_DOMAIN_SENSOR_BASE + 0x06u)) /**< 冗余通道不一致 */

#ifdef __cplusplus
}
#endif

#endif /* BM_FAULT_H */
