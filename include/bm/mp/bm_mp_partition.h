/**
 * @file bm_mp_partition.h
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @brief MP 闭源扩展公共 API · 需 bm_mp
 *
 * `bm_mp_partition_build_and_validate()` 在 Bootstrap 阶段根据注册表与资源拓扑
 * 生成 event/module owner 映射及 `partition_crc`。
 *
 * @warning 缓存一致性前提（P1-10）：内部全局表（`s_event_owner`/`s_module_owner`/
 *          `s_event_name`）位于普通 BSS，从核经 boot_phase acquire 读取。此路径
 *          **仅在缓存一致性多核硬件上成立**；非相干 AMP 上这些表无 cache 维护
 *          （不同于 IPC 矩阵/relay 有 non-cacheable 放置与 cache 操作），且当前
 *          **无编译期护栏**（无 `#error`）阻止误用。非相干 AMP 目标须自行将分区表
 *          置于相干/非缓存内存并做 cache 维护，否则从核可能读到陈旧 owner 映射。
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
#ifndef BM_MP_PARTITION_H
#define BM_MP_PARTITION_H

#include "bm/mp/bm_mp_types.h"
#include "bm/common/bm_types.h"
#include "bm/core/bm_event.h"

#define BM_MP_PARTITION_LAYOUT_VERSION  1u

/** 启动完成后只读的分区快照 */
typedef struct {
    uint32_t        cpu_count;
    uint32_t        layout_version;
    uint32_t        partition_crc;
    const uint8_t  *event_owner;   /**< [BM_CONFIG_MAX_EVENT_TYPES] */
    const uint8_t  *module_owner;  /**< [module 表长度] */
} bm_mp_partition_t;

/**
 * @brief 构建并校验静态分区表
 *
 * @return BM_OK 成功；负值为装配或 owner 闭包错误
 */
int bm_mp_partition_build_and_validate(void);

/**
 * @brief 获取当前只读分区表
 *
 * @return 分区表指针；未 build 时可能为 NULL
 */
const bm_mp_partition_t *bm_mp_partition(void);

/**
 * @brief 查询事件类型的 owner CPU
 *
 * @param type 事件类型 ID
 * @return owner CPU；类型无效时返回 0
 */
uint8_t bm_mp_event_owner(bm_event_type_t type);

/**
 * @brief 查询模块表条目的 owner CPU
 *
 * @param module_index 模块表索引
 * @return owner CPU
 */
uint8_t bm_mp_module_owner(uint32_t module_index);

/** 预指定事件类型的 owner CPU（须在 partition build 前调用） */
int bm_mp_partition_register_event_owner(bm_event_type_t type,
                                         const char *name,
                                         uint8_t owner_cpu);

/** 声明 module 订阅的 event（build 时校验 module.owner == event.owner） */
int bm_mp_partition_register_module_event(uint32_t module_index,
                                        bm_event_type_t type);

void bm_mp_partition_reset(void);
int bm_mp_partition_register_events_on_this_cpu(void);

#endif /* BM_MP_PARTITION_H */
