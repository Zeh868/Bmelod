/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_nvs_stm32g4.h
 * @brief STM32G4 Flash 双槽 NVS：Board 注入分区布局
 * @maturity E1
 *
 * 调用 `bm_nvs_stm32g4_set_layout()` 后再使用 `bm_persist_init/commit`。
 *
 * 布局约束（由本后端强制校验，非法布局返回 BM_ERR_INVALID）：
 * - 槽 A/B 基地址均须按当前主 Flash 页大小对齐（双 Bank 2KB，单 Bank 4KB）；
 * - `slot_size` 须为页大小整数倍，且 `<= BM_NVS_STM32G4_SLOT_BUF_MAX`；
 * - 两槽实际擦除页区间不得重叠；
 * - 两槽须完全落在有效主 Flash 地址范围内；
 * - `slot_size >= bm_nvs_slot_min_size(persist_blob_size)`。
 *
 * App 链接脚本必须显式保留这两段 Flash 空间，避免代码/数据被擦除。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 G4 Flash NVS 布局注入
 * 2026-07-29       1.1            zeh            明确页对齐、整数倍、不重叠、有效 Flash 范围约束
 *
 * 2026-08-01       1.1            Codex            补全中文 Doxygen 合规注释
 */
#ifndef BM_HAL_NVS_STM32G4_H
#define BM_HAL_NVS_STM32G4_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注入双槽 Flash 分区（Board 在 persist_init 前调用一次）
 *
 * @param base_a     槽 A 起始地址（须按当前页大小对齐，落在主 Flash）
 * @param base_b     槽 B 起始地址（同上，且与 A 区间不重叠）
 * @param slot_size  单槽字节数（两端相同；须为页大小整数倍）
 * @return BM_OK；BM_ERR_INVALID 对齐/重叠/地址非法/超出 Flash 容量
 */
int bm_nvs_stm32g4_set_layout(uint32_t base_a, uint32_t base_b,
                              uint32_t slot_size);

#ifdef __cplusplus
}
#endif

#endif /* BM_HAL_NVS_STM32G4_H */
