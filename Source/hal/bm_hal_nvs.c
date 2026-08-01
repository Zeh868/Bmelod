/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_nvs.c
 * @brief NVS 无后端 fail-closed 桩（宏守卫，非弱符号）
 *
 * 机制：全文件以 `#ifndef BM_DRV_HAS_NVS_BACKEND` 守卫。三个 NVS pack
 * （native_sim / native_sim_mp / sdk_stm32g4）均已向 bm_hal 注入该宏
 * （与 bm_persist.c 编译期门同一宏通道），有后端时本文件编空，
 * 后端强符号唯一——无重复符号、无静态库链接顺序依赖，MSVC/GNU 全
 * 工具链安全；宏未注入（无 NVS 后端的配置）时桩生效，应用直接调
 * `bm_hal_nvs_load/save` 得到运行期 `BM_ERR_NOT_INIT`
 * （原链接期报错 → fail-closed，与其它外设分发层口径一致）。
 *
 * `bm_persist.c` 的编译期门与 `BM_ERR_NOT_SUPPORTED` 返回不在此改动
 * （见 2026-08-01 HAL 实例模型补完规格 2.3）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-08-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-08-01       1.0            zeh            新增（P2 NVS fail-closed 宏守卫桩）
 *
 */
#ifndef BM_DRV_HAS_NVS_BACKEND

#include "bm_hal_nvs.h"
#include "bm_types.h"

int bm_hal_nvs_load(uint8_t *buf, uint16_t size) {
    (void)buf;
    (void)size;
    return BM_ERR_NOT_INIT;
}

int bm_hal_nvs_save(const uint8_t *buf, uint16_t size) {
    (void)buf;
    (void)size;
    return BM_ERR_NOT_INIT;
}

#endif /* BM_DRV_HAS_NVS_BACKEND */
