/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_spi_stm32g4.h
 * @brief STM32G474xB SPI1 设备声明（bm_drv_spi 契约）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1）
 *
 */
#ifndef BM_VENDOR_SPI_STM32G4_H
#define BM_VENDOR_SPI_STM32G4_H

#include "bm_hal_spi.h"

/** @brief STM32G4 SPI1 设备（默认 config 见 bm_vendor_spi_stm32g4.c，
 *        时钟/模式/CS 由 bm_spi_config_t 覆盖）。 */
extern const bm_hal_spi_t bm_stm32g4_spi1;

/** @brief SPI1 默认配置（instances 宏取值；应用可自建 bm_spi_config_t 覆盖）。 */
extern const bm_spi_config_t bm_stm32g4_spi1_default_config;

#endif /* BM_VENDOR_SPI_STM32G4_H */
