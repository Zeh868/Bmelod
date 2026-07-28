/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_board.c
 * @brief Board 接入契约实现
 *
 * 维护应用注入的 Board 设备表，提供注册、能力查询、设备查找与
 * 资源冲突检查。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.4
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 Board 接入契约实现
 * 2026-07-28       1.1            zeh            增加资源数组冲突检查与 init_devices
 * 2026-07-28       1.2            zeh            MSG_RAM 冲突补 periph_id 比较与
 *                                                端点溢出防御；名称上限提取宏
 * 2026-07-28       1.3            zeh            MSG_RAM 按全局 word 区间重叠判冲突
 * 2026-07-28       1.4            zeh            增加 register_devices；旧名保留
 *
 */
#include "board/bm_board.h"
#include "board/bm_board_device.h"

#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/*  静态状态                                                                   */
/* -------------------------------------------------------------------------- */

static const bm_board_table_t *s_board_table = NULL;

/* -------------------------------------------------------------------------- */
/*  内部辅助                                                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief 检查设备类型是否合法
 */
static int bm_board_type_is_valid(uint32_t type) {
    switch (type) {
    case BM_BOARD_DEV_TYPE_UART:
    case BM_BOARD_DEV_TYPE_SPI:
    case BM_BOARD_DEV_TYPE_CAN:
    case BM_BOARD_DEV_TYPE_TIMER:
    case BM_BOARD_DEV_TYPE_GPIO:
    case BM_BOARD_DEV_TYPE_NVS:
    case BM_BOARD_DEV_TYPE_PWM:
    case BM_BOARD_DEV_TYPE_ADC:
        return 1;
    default:
        return 0;
    }
}

/**
 * @brief 由设备类型推导能力位
 */
static uint32_t bm_board_cap_from_type(uint32_t type) {
    switch (type) {
    case BM_BOARD_DEV_TYPE_UART:  return BM_CAP_UART;
    case BM_BOARD_DEV_TYPE_SPI:   return BM_CAP_SPI;
    case BM_BOARD_DEV_TYPE_CAN:   return BM_CAP_CAN;
    case BM_BOARD_DEV_TYPE_TIMER: return BM_CAP_TIMER;
    case BM_BOARD_DEV_TYPE_GPIO:  return BM_CAP_GPIO;
    case BM_BOARD_DEV_TYPE_NVS:   return BM_CAP_NVS;
    case BM_BOARD_DEV_TYPE_PWM:   return BM_CAP_PWM;
    case BM_BOARD_DEV_TYPE_ADC:   return BM_CAP_ADC;
    default:                      return 0u;
    }
}

/**
 * @brief 计算两个以 NUL 结尾的字符串是否相等（允许 NULL 参数）
 */
static int bm_board_str_equal(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return (a == b) ? 1 : 0;
    }
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (*a == *b) ? 1 : 0;
}

/**
 * @brief 计算字符串长度（上限保护）
 */
static size_t bm_board_str_len(const char *s, size_t max) {
    size_t n = 0u;
    while (n < max && s[n] != '\0') {
        n++;
    }
    return n;
}

/**
 * @brief 检查两条资源是否冲突
 *
 * 冲突规则：
 * - PIN：同端口同引脚。
 * - DMA：同控制器同通道。
 * - IRQ：同 IRQn 编号。
 * - TIMER_CH：同 TIM 实例同通道。
 * - MSG_RAM：全局 Message RAM 的 [index, index+flags) 区间重叠即冲突
 *   （STM32G4 上 FDCAN1/2 共享同一块 RAM；periph_id 仅文档）。flags==0 或
 *   index+flags 溢出 uint32 的非法区间视为冲突。
 * - AF：同 AF 编号不视为冲突（多引脚可共用 AF）。
 */
static int bm_board_resource_conflicts(const bm_board_resource_t *a,
                                       const bm_board_resource_t *b) {
    if (a->type != b->type) {
        return 0;
    }

    switch (a->type) {
    case BM_BOARD_RES_PIN:
    case BM_BOARD_RES_DMA:
    case BM_BOARD_RES_IRQ:
    case BM_BOARD_RES_TIMER_CH:
        return (a->periph_id == b->periph_id && a->index == b->index) ? 1 : 0;

    case BM_BOARD_RES_MSG_RAM: {
        uint32_t a_start;
        uint32_t a_end;
        uint32_t b_start;
        uint32_t b_end;

        /* 端点溢出防御：零长度或 index+flags 溢出的区间非法，视为冲突 */
        if (a->flags == 0u || a->index > UINT32_MAX - a->flags ||
            b->flags == 0u || b->index > UINT32_MAX - b->flags) {
            return 1;
        }
        a_start = a->index;
        a_end   = a_start + a->flags;
        b_start = b->index;
        b_end   = b_start + b->flags;

        return (a_start < b_end && b_start < a_end) ? 1 : 0;
    }

    case BM_BOARD_RES_AF:
    default:
        return 0;
    }
}

/**
 * @brief 检查两个设备的资源数组是否存在冲突
 */
static int bm_board_device_resources_conflict(const bm_board_device_t *a,
                                              const bm_board_device_t *b) {
    uint32_t i;
    uint32_t j;

    if (a->resources == NULL || a->resource_count == 0u ||
        b->resources == NULL || b->resource_count == 0u) {
        return 0;
    }

    for (i = 0u; i < a->resource_count; i++) {
        for (j = 0u; j < b->resource_count; j++) {
            if (bm_board_resource_conflicts(&a->resources[i],
                                            &b->resources[j]) != 0) {
                return 1;
            }
        }
    }
    return 0;
}

/**
 * @brief 检查已注册表中是否存在冲突（公共实现）
 */
static int bm_board_check_conflicts_impl(const bm_board_table_t *table) {
    uint32_t i;
    uint32_t j;

    if (table == NULL) {
        return BM_ERR_INVALID;
    }

    for (i = 0u; i < table->count; i++) {
        const bm_board_device_t *a = &table->devices[i];

        for (j = i + 1u; j < table->count; j++) {
            const bm_board_device_t *b = &table->devices[j];

            if (a->type == b->type && a->instance == b->instance) {
                return BM_ERR_INVALID;
            }
            if (a->resource_tag != 0u &&
                a->resource_tag == b->resource_tag) {
                return BM_ERR_INVALID;
            }
            if (a->name != NULL && b->name != NULL &&
                bm_board_str_equal(a->name, b->name)) {
                return BM_ERR_INVALID;
            }
            if (bm_board_device_resources_conflict(a, b) != 0) {
                return BM_ERR_INVALID;
            }
        }
    }
    return BM_OK;
}

/* -------------------------------------------------------------------------- */
/*  公共 API                                                                   */
/* -------------------------------------------------------------------------- */

int bm_board_register(const bm_board_table_t *table) {
    uint32_t i;

    if (s_board_table != NULL) {
        return BM_ERR_ALREADY;
    }
    if (table == NULL || table->devices == NULL || table->count == 0u) {
        return BM_ERR_INVALID;
    }

    /* 逐项基础校验 */
    for (i = 0u; i < table->count; i++) {
        const bm_board_device_t *dev = &table->devices[i];

        if (!bm_board_type_is_valid(dev->type)) {
            return BM_ERR_INVALID;
        }
        if (dev->hal_dev == NULL) {
            return BM_ERR_INVALID;
        }
        if (dev->name != NULL &&
            bm_board_str_len(dev->name, BM_BOARD_NAME_MAX) == BM_BOARD_NAME_MAX) {
            /* 设备名过长，避免无界比较 */
            return BM_ERR_INVALID;
        }
    }

    if (bm_board_check_conflicts_impl(table) != BM_OK) {
        return BM_ERR_INVALID;
    }

    s_board_table = table;
    return BM_OK;
}

int bm_board_has_capability(uint32_t cap) {
    if (cap == 0u) {
        return 0;
    }
    return (bm_board_capability_mask() & cap) == cap ? 1 : 0;
}

uint32_t bm_board_capability_mask(void) {
    uint32_t mask = 0u;
    uint32_t i;

    if (s_board_table == NULL) {
        return 0u;
    }

    mask = s_board_table->capabilities;
    for (i = 0u; i < s_board_table->count; i++) {
        mask |= bm_board_cap_from_type(s_board_table->devices[i].type);
    }
    return mask;
}

const bm_board_device_t *bm_board_find(uint32_t type, uint32_t instance) {
    uint32_t i;

    if (s_board_table == NULL || !bm_board_type_is_valid(type)) {
        return NULL;
    }
    for (i = 0u; i < s_board_table->count; i++) {
        const bm_board_device_t *dev = &s_board_table->devices[i];

        if (dev->type == type && dev->instance == instance) {
            return dev;
        }
    }
    return NULL;
}

const bm_board_device_t *bm_board_find_by_name(const char *name) {
    uint32_t i;

    if (s_board_table == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0u; i < s_board_table->count; i++) {
        const bm_board_device_t *dev = &s_board_table->devices[i];

        if (dev->name != NULL && bm_board_str_equal(dev->name, name)) {
            return dev;
        }
    }
    return NULL;
}

int bm_board_check_conflicts(void) {
    return bm_board_check_conflicts_impl(s_board_table);
}

int bm_board_register_devices(const bm_board_device_t *devices, uint32_t count,
                              uint32_t capabilities) {
    static bm_board_table_t table;

    if (devices == NULL || count == 0u) {
        return BM_ERR_INVALID;
    }

    table.devices = devices;
    table.count = count;
    table.capabilities = capabilities;
    return bm_board_register(&table);
}

int bm_board_init_devices(const bm_board_device_t *devices, uint32_t count,
                          uint32_t capabilities) {
    return bm_board_register_devices(devices, count, capabilities);
}

uint32_t bm_board_device_count(void) {
    if (s_board_table == NULL) {
        return 0u;
    }
    return s_board_table->count;
}
