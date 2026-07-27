/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file abs_encoder.h
 * @brief 绝对值编码器组件（型号 vtable + AS5047P SPI 实现）
 *
 * 业务只在 config 选 vtable，换型号不改业务。AS5047P 实现架在 bm_hal_spi
 * 上：16 位命令/数据帧 + 偶校验（bit15）、错误位（bit14）、14 位角度，
 * 帧格式按 AS5047P datasheet（ams）“Serial Peripheral Interface”章节。
 *
 * @par 多型号扩展（机制说明，无需改本文件）
 * 新增型号 = 新增一个实现同一张 bm_abs_encoder_api vtable 的 .c 文件
 * （提供 `const bm_abs_encoder_api_t bm_abs_encoder_<型号>_api` 与其
 * config 结构），业务在设备构造处换 api/config 指针即可：
 * @code
 *   bm_abs_encoder_mt6816_config_t cfg = { .spi = &my_spi };
 *   bm_hal_abs_encoder_t enc = { &bm_abs_encoder_mt6816_api, &cfg };
 *   bm_abs_encoder_read_angle(&enc, &raw);   // 调用面不变
 * @endcode
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1 步进伺服栈）
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_ABS_ENCODER_H
#define BM_ABS_ENCODER_H

#include "bm_hal_spi.h"
#include "bm/common/bm_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct bm_hal_abs_encoder;

/**
 * @brief 绝对编码器型号 vtable。
 */
typedef struct bm_abs_encoder_api {
    /**
     * @brief 读角度原始值（AS5047P 为 14bit，0..16383）
     * @return BM_OK 成功；否则为平台/协议错误码
     */
    int (*read_angle)(const struct bm_hal_abs_encoder *dev, uint16_t *raw);
    /**
     * @brief 读状态字（bit15=帧错误位，bit1=MAGH 磁强过高，bit0=MAGL 过低）
     * @return BM_OK 成功；否则为平台/协议错误码
     */
    int (*read_status)(const struct bm_hal_abs_encoder *dev, uint16_t *status);
} bm_abs_encoder_api_t;

/**
 * @brief 绝对编码器设备（业务聚合点：{api, config}）。
 */
typedef struct bm_hal_abs_encoder {
    const bm_abs_encoder_api_t *api;
    const void                 *config;
} bm_hal_abs_encoder_t;

/** @brief AS5047P 设备配置：SPI 设备实例（CS 管理由 bm_spi_config_t 承担）。 */
typedef struct {
    const bm_hal_spi_t *spi;
} bm_abs_encoder_as5047p_config_t;

/** @brief AS5047P 型号 vtable（业务构造设备：{&bm_abs_encoder_as5047p_api, &cfg}）。 */
extern const bm_abs_encoder_api_t bm_abs_encoder_as5047p_api;

/* ---------- 薄分发（型号无关） ---------- */

/**
 * @brief 读角度原始值
 * @param dev 编码器设备
 * @param raw 输出角度原始值
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；否则为平台/协议错误码
 */
int bm_abs_encoder_read_angle(const bm_hal_abs_encoder_t *dev, uint16_t *raw);

/**
 * @brief 读状态字（bit15=帧错误，bit1=MAGH，bit0=MAGL）
 * @param dev    编码器设备
 * @param status 输出状态字
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；否则为平台/协议错误码
 */
int bm_abs_encoder_read_status(const bm_hal_abs_encoder_t *dev, uint16_t *status);

#ifdef __cplusplus
}
#endif

#endif /* BM_ABS_ENCODER_H */
