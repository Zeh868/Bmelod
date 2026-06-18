/**
 * @file bm_resource.h
 * @brief 可扩展硬件资源声明与冲突检测
 *
 * 多实例通过 bm_resource_claim_t 声明外设与流端点访问模式，启动前由框架校验
 * 跨实例冲突。资源类别使用 bm_resource_class_t 可扩展 ID，内置 HAL 类与
 * 应用扩展区分离。详见 docs/06-路线图与多领域/02-多领域确定性流式架构.md §5.4。
 *
 * @author zeh (china_qzh@163.com)
 * @version 2.0
 * @date 2026-06-12
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-12       2.0            zeh            resource_class 可扩展 ID
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_RESOURCE_H
#define BM_RESOURCE_H

#include "bm/common/bm_types.h"

#include <stdint.h>

typedef uint16_t bm_resource_class_t;

#define BM_RESOURCE_CLASS_CORE_BASE        0x0000u
#define BM_RESOURCE_CLASS_HAL_BASE         0x0100u
#define BM_RESOURCE_CLASS_APP_BASE         0x8000u

#define BM_RESOURCE_CLASS_PWM              (BM_RESOURCE_CLASS_HAL_BASE + 0x01u)
#define BM_RESOURCE_CLASS_ADC_GROUP        (BM_RESOURCE_CLASS_HAL_BASE + 0x02u)
#define BM_RESOURCE_CLASS_TIMER            (BM_RESOURCE_CLASS_HAL_BASE + 0x03u)
#define BM_RESOURCE_CLASS_DMA_CHANNEL      (BM_RESOURCE_CLASS_HAL_BASE + 0x04u)
#define BM_RESOURCE_CLASS_GPIO             (BM_RESOURCE_CLASS_HAL_BASE + 0x05u)
#define BM_RESOURCE_CLASS_COMP             (BM_RESOURCE_CLASS_HAL_BASE + 0x06u)
#define BM_RESOURCE_CLASS_IRQ              (BM_RESOURCE_CLASS_HAL_BASE + 0x07u)
#define BM_RESOURCE_CLASS_STREAM_ENDPOINT  (BM_RESOURCE_CLASS_HAL_BASE + 0x10u)
#define BM_RESOURCE_CLASS_CLOCK_DOMAIN     (BM_RESOURCE_CLASS_HAL_BASE + 0x11u)
#define BM_RESOURCE_CLASS_MEMORY_REGION    (BM_RESOURCE_CLASS_HAL_BASE + 0x12u)
#define BM_RESOURCE_CLASS_ACCELERATOR      (BM_RESOURCE_CLASS_HAL_BASE + 0x13u)

#define BM_RESOURCE_CLASS_LAST_BUILTIN     BM_RESOURCE_CLASS_ACCELERATOR

/** 资源访问模式 */
typedef enum {
    BM_RESOURCE_EXCLUSIVE,
    BM_RESOURCE_OWNER,
    BM_RESOURCE_SHARED_READ,
    BM_RESOURCE_SHARED_COORDINATED
} bm_resource_access_t;

typedef struct {
    bm_resource_class_t resource_class;
    uintptr_t key;
    bm_resource_access_t access;
    uintptr_t share_group;
    const char *name;
} bm_resource_claim_t;

int bm_resource_check_conflicts(const bm_resource_claim_t *const *claims,
                                const uint32_t *claim_counts,
                                uint32_t instance_count);

int bm_resource_class_valid(bm_resource_class_t resource_class);

#endif /* BM_RESOURCE_H */
