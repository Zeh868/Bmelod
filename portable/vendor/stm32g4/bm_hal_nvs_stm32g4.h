/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_nvs_stm32g4.h
 * @brief STM32G4 Flash 双槽 NVS：Board 注入分区布局
 *
 * 调用 `bm_nvs_stm32g4_set_layout()` 后再使用 `bm_persist_init/commit`。
 * 两槽须位于主 Flash，互不重叠；`slot_size` 建议为页大小整数倍（G474 常 2KB），
 * 且 `slot_size >= bm_nvs_slot_min_size(persist_blob_size)`。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 G4 Flash NVS 布局注入
 *
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
 * @param base_a     槽 A 起始地址（须 8 字节对齐，落在主 Flash）
 * @param base_b     槽 B 起始地址（同上，且与 A 区间不重叠）
 * @param slot_size  单槽字节数（两端相同；建议页对齐）
 * @return BM_OK；BM_ERR_INVALID 对齐/重叠/地址非法
 */
int bm_nvs_stm32g4_set_layout(uint32_t base_a, uint32_t base_b,
                              uint32_t slot_size);

#ifdef __cplusplus
}
#endif

#endif /* BM_HAL_NVS_STM32G4_H */
