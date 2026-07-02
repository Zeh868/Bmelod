/**
 * @file bm_mp_resource_topology.h
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @brief MP 闭源扩展公共 API · 需 bm_mp
 *
 * 分区器依据只读拓扑表验证 `owner_cpu`、IRQ、DMA 与 claim mask 的交集恰为一位。
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
#ifndef BM_MP_RESOURCE_TOPOLOGY_H
#define BM_MP_RESOURCE_TOPOLOGY_H

#include "bm/mp/bm_mp_types.h"
#include "bm/hybrid/bm_resource.h"
#include "bm/common/bm_types.h"

/*
 * 真源在 bm_config.h（BM_CONFIG_RESOURCE_TOPOLOGY_MAX），此处仅 MP 名别名桥。
 * 方向统一为 bm_config → mp，保证用户覆盖 bm_config 旋钮对 mp 拓扑表生效。
 */
#ifndef BM_CONFIG_MP_RESOURCE_TOPOLOGY_MAX
#define BM_CONFIG_MP_RESOURCE_TOPOLOGY_MAX  BM_CONFIG_RESOURCE_TOPOLOGY_MAX
#endif

/** 平台只读资源拓扑条目（由 BSP 或链接脚本注册） */
typedef struct {
    bm_resource_class_t resource_class;
    uintptr_t           key;
    uint32_t            allowed_cpu_mask;
    uint32_t            irq_number;
    uint32_t            memory_region_mask;
} bm_mp_resource_topology_entry_t;

void bm_mp_resource_topology_reset(void);

/**
 * @brief 注册资源拓扑条目（partition build 前）
 *
 * @param entry 拓扑描述
 * @return BM_OK 成功
 */
int bm_mp_resource_topology_register(
    const bm_mp_resource_topology_entry_t *entry);

/**
 * @brief 校验 claim 的 allowed CPU 与实例 owner 一致
 *
 * @param owner_cpu 实例 owner
 * @param claim 资源声明
 * @return BM_OK 闭包成立
 */
int bm_mp_resource_topology_validate_claim(uint8_t owner_cpu,
                                           const bm_resource_claim_t *claim);

/**
 * @brief 校验拓扑表内 IRQ 与 mask 一致性
 *
 * @return BM_OK 通过
 */
int bm_mp_resource_topology_validate_table(void);

#if BM_CONFIG_ENABLE_EXEC
#include "bm/hybrid/bm_exec.h"

/**
 * @brief 校验 exec 实例表与 stream/claim 亲和闭包
 */
int bm_mp_resource_topology_validate_exec_table(
    const bm_exec_t *const *instances, uint32_t count);
#endif

#endif /* BM_MP_RESOURCE_TOPOLOGY_H */
