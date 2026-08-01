/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_nvs.h
 * @brief 非易失存储（NVS）后端接口（路线图 #10）
 *
 * 各 portable/sim/ 后端实现 `bm_hal_nvs_load()` / `bm_hal_nvs_save()`；
 * 核心层 `bm_persist.c` 在此之上提供 KV 持久化公共 API。
 *
 * 契约：
 * - `bm_hal_nvs_load(buf, size)`：将已持久化数据读入 buf；
 *   若无历史数据（首次上电）返回 BM_ERR_NOT_FOUND；
 * - `bm_hal_nvs_save(buf, size)`：将 buf 中 size 字节原子写入 NVS；
 * - 两者均为同步调用，完成后方才返回；
 * - 嵌入式 flash 后端须自行处理擦除-写入时序；STM32G4 提供双槽 Flash
 *   实现（`bm_drv_nvs_flash_stm32g4.c`），Board 经 `bm_nvs_stm32g4_set_layout`
 *   注入分区后再 commit。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-26       1.0            zeh            正式发布（路线图 #10 参数/配置持久化）
 * 2026-07-28       1.1            zeh            注明 STM32G4 双槽 Flash 后端与布局注入
 * 2026-08-01       1.2            zeh            P2：补无后端 fail-closed 宏守卫桩
 *                                               （Source/hal/bm_hal_nvs.c，与 bm_persist
 *                                               同一 BM_DRV_HAS_NVS_BACKEND 宏通道）
 *
 */
#ifndef BM_HAL_NVS_H
#define BM_HAL_NVS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 从 NVS 后端读取原始字节块
 *
 * 由 portable 后端实现：
 * - native_sim：读取指定路径的二进制文件；
 * - STM32G4：从双槽 Flash 中选 active 槽读出载荷。
 *
 * @param buf  目标缓冲区（非 NULL，长度 >= size）
 * @param size 需要读取的字节数
 * @return BM_OK 成功；BM_ERR_NOT_FOUND 无历史数据（首次上电）；
 *         BM_ERR_INVALID 数据长度不符；BM_ERR_NOT_INIT 后端未配置
 */
int bm_hal_nvs_load(uint8_t *buf, uint16_t size);

/**
 * @brief 向 NVS 后端写入原始字节块
 *
 * 由 portable/sim/ 后端实现。写入完成且持久化后返回。
 *
 * @param buf  源数据缓冲区（非 NULL，长度 >= size）
 * @param size 写入字节数
 * @return BM_OK 成功；BM_ERR_BUSY 后端暂不可用；
 *         BM_ERR_OVERFLOW 写入不完整；BM_ERR_NOT_INIT 后端未配置
 */
int bm_hal_nvs_save(const uint8_t *buf, uint16_t size);

#ifdef __cplusplus
}
#endif

#endif /* BM_HAL_NVS_H */
