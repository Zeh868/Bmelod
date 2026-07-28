/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_board.h
 * @brief Board 接入契约：注册、能力查询与资源冲突检查
 *
 * 应用在启动阶段调用 `bm_board_register()` 注入静态定义的外设实例表；
 * 框架与组件通过 `bm_board_find()` 与 `bm_board_has_capability()` 查询
 * 设备与能力。后端缺失时能力查询返回 0，调用方须返回明确错误码
 *（如 `BM_ERR_NOT_SUPPORTED`）。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 Board 接入契约
 * 2026-07-28       1.1            zeh            增加 bm_board_init_devices()
 *
 */
#ifndef BM_BOARD_H
#define BM_BOARD_H

#include "board/bm_board_device.h"

#include "bm/common/bm_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册 Board 外设表
 *
 * 启动期由应用调用一次；重复调用返回 `BM_ERR_ALREADY`。
 * 注册时执行以下检查：
 * - table / devices 非空且 count > 0；
 * - 每个设备的 type 合法、hal_dev 非空；
 * - 同一 type 内 instance 唯一；
 * - 非零 resource_tag 全局唯一；
 * - 非空 name 全局唯一。
 *
 * @param table Board 设备表（应用静态分配，生命周期须覆盖整个运行期）
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法或冲突；BM_ERR_ALREADY 已注册
 */
int bm_board_register(const bm_board_table_t *table);

/**
 * @brief 便捷注册：由设备数组与能力掩码直接注册 Board
 *
 * 等价于构造 `bm_board_table_t` 后调用 `bm_board_register()`。
 *
 * @param devices     设备数组（应用静态分配，生命周期须覆盖整个运行期）
 * @param count       设备数量，须 > 0
 * @param capabilities Board 级显式能力掩码
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法或冲突；BM_ERR_ALREADY 已注册
 */
int bm_board_init_devices(const bm_board_device_t *devices, uint32_t count,
                          uint32_t capabilities);

/**
 * @brief 查询 Board 是否具备某项能力
 *
 * 能力来自两部分：应用显式声明的 `table->capabilities`，以及框架根据
 * 已注册设备类型自动推导的能力。未注册或后端缺失时返回 0。
 *
 * @param cap 能力位掩码，见 BM_CAP_*
 * @return 非零表示具备该能力；0 表示不具备
 */
int bm_board_has_capability(uint32_t cap);

/**
 * @brief 返回当前 Board 能力掩码
 *
 * @return 已注册 board 的能力位掩码；未注册时返回 0
 */
uint32_t bm_board_capability_mask(void);

/**
 * @brief 按类型和实例号查找设备
 *
 * @param type     设备类型，见 BM_BOARD_DEV_TYPE_*
 * @param instance 应用分配的逻辑实例号
 * @return 指向设备描述符的指针；未找到时返回 NULL
 */
const bm_board_device_t *bm_board_find(uint32_t type, uint32_t instance);

/**
 * @brief 按名称查找设备
 *
 * @param name 设备名（非 NULL）
 * @return 指向设备描述符的指针；未找到时返回 NULL
 */
const bm_board_device_t *bm_board_find_by_name(const char *name);

/**
 * @brief 检查已注册表中是否存在资源冲突
 *
 * 通常由 `bm_board_register()` 内部调用，也可单独用于启动前自检。
 *
 * @return BM_OK 无冲突；BM_ERR_INVALID 存在冲突或尚未注册
 */
int bm_board_check_conflicts(void);

/**
 * @brief 获取已注册设备总数
 *
 * @return 设备数量；未注册时返回 0
 */
uint32_t bm_board_device_count(void);

#ifdef __cplusplus
}
#endif

#endif /* BM_BOARD_H */
