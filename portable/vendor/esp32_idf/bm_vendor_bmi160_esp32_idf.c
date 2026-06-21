/**
 * @file bm_vendor_bmi160_esp32_idf.c
 * @brief ESP32-WROOM-32E vendor 专用 BMI160 硬件 I2C 实现（LL 裸机）
 *
 * BMI160 与 M0 AS5600 共用 I2C_NUM_1（SDA=GPIO19, SCL=GPIO18），
 * 实际板级地址为 0x69（SDO/SA0 接 VDDIO）。
 * 本实现通过 bm_vendor_i2c_esp32_idf 的 LL 寄存器级接口通信，
 * 零 FreeRTOS 依赖；复位/上电等待用 esp_rom_delay_us 实现有界忙等。
 *
 * @author zeh (china_qzh@163.com)
 * @version 2.1
 * @date 2026-06-21
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            新增 BMI160 vendor 入口
 * 2026-06-19       1.1            zeh          删除总线假实现，仅保留明确返回
 * 2026-06-21       2.0            zeh           改用 ESP-IDF 硬件 I2C 实现
 * 2026-06-21       2.1            zeh         删除 FreeRTOS 依赖，vTaskDelay→esp_rom_delay_us
 */
#include "bm_vendor_bmi160_esp32_idf.h"
#include "bm_vendor_i2c_esp32_idf.h"
#include "bm_hal_instances_esp32wroom32e.h"
#include "bm_types.h"

#include <string.h>

#include "esp_rom_sys.h"

/** @brief BMI160 数据寄存器 burst 长度。 */
#define BMI160_DATA_LEN  12u

/**
 * @brief 规范化配置。
 */
static void bm_vendor_bmi160_normalize_config(const bm_vendor_bmi160_config_t *cfg,
                                              bm_vendor_bmi160_config_t *out) {
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (cfg == NULL) {
        return;
    }
    *out = *cfg;
    if (out->address == 0u) {
        out->address = BM_VENDOR_BMI160_I2C_ADDR_DEFAULT;
    }
    if (out->clock_hz == 0u) {
        /*
         * I2C1 与 M0 AS5600 共线。电机出波时 400 kHz 已观测到地址 NACK 和
         * SDA 卡低；100 kHz 足以覆盖当前 100 Hz 采样并显著增加抗干扰余量。
         */
        out->clock_hz = (out->bus_type == BM_VENDOR_BMI160_BUS_SPI) ? 1000000u : 100000u;
    }
    if (out->acc_conf == 0u) {
        out->acc_conf = BM_VENDOR_BMI160_DEFAULT_ACC_CONF;
    }
    if (out->acc_range == 0u) {
        out->acc_range = BM_VENDOR_BMI160_DEFAULT_ACC_RANGE;
    }
    if (out->gyr_conf == 0u) {
        out->gyr_conf = BM_VENDOR_BMI160_DEFAULT_GYR_CONF;
    }
    if (out->gyr_range == 0u) {
        out->gyr_range = BM_VENDOR_BMI160_DEFAULT_GYR_RANGE;
    }
}

/**
 * @brief BMI160 运行时句柄。
 */
struct bm_vendor_bmi160_handle {
    /** @brief 规范化后的配置。 */
    bm_vendor_bmi160_config_t config;
    /** @brief 是否已标记为可用。 */
    int initialized;
};

/**
 * @brief 向 BMI160 写单个寄存器。
 */
static int bm_vendor_bmi160_write_reg(const bm_vendor_bmi160_handle_t *handle,
                                      uint8_t reg, uint8_t value) {
    const bm_vendor_bmi160_config_t *cfg;
    uint8_t buf[2];

    if (handle == NULL) {
        return BM_ERR_INVALID;
    }
    cfg = &handle->config;
    if (cfg->bus_type != BM_VENDOR_BMI160_BUS_I2C) {
        return BM_ERR_NOT_SUPPORTED;
    }

    buf[0] = reg;
    buf[1] = value;
    return bm_vendor_i2c_write((i2c_port_t)cfg->bus_id, cfg->address,
                               buf, 2u, 0u);
}

/**
 * @brief 从 BMI160 读多个寄存器。
 */
static int bm_vendor_bmi160_read_regs(const bm_vendor_bmi160_handle_t *handle,
                                      uint8_t reg, uint8_t *buf, size_t len) {
    const bm_vendor_bmi160_config_t *cfg;

    if (handle == NULL || buf == NULL || len == 0u) {
        return BM_ERR_INVALID;
    }
    cfg = &handle->config;
    if (cfg->bus_type != BM_VENDOR_BMI160_BUS_I2C) {
        return BM_ERR_NOT_SUPPORTED;
    }

    return bm_vendor_i2c_write_read((i2c_port_t)cfg->bus_id, cfg->address,
                                    &reg, 1u, buf, len, 0u);
}

/**
 * @brief 初始化 BMI160 句柄。
 */
int bm_vendor_bmi160_init(bm_vendor_bmi160_handle_t *handle,
                          const bm_vendor_bmi160_config_t *config) {
    bm_vendor_bmi160_config_t cfg;
    int rc;

    if (handle == NULL || config == NULL) {
        return BM_ERR_INVALID;
    }
    memset(handle, 0, sizeof(*handle));
    bm_vendor_bmi160_normalize_config(config, &cfg);

    if (cfg.bus_type != BM_VENDOR_BMI160_BUS_I2C) {
        return BM_ERR_NOT_SUPPORTED;
    }

    /* 初始化共享 I2C 端口（与 M0 AS5600 共用）。 */
    rc = bm_vendor_i2c_port_init((i2c_port_t)cfg.bus_id,
                                 (gpio_num_t)cfg.sda_gpio,
                                 (gpio_num_t)cfg.scl_gpio,
                                 cfg.clock_hz);
    if (rc != BM_OK) {
        return rc;
    }

    handle->config = cfg;

    /* 软复位；BMI160 datasheet 要求复位后等待 ≥ 1 ms（保守取 10 ms）。 */
    rc = bm_vendor_bmi160_write_reg(handle, BM_VENDOR_BMI160_REG_CMD,
                                    BM_VENDOR_BMI160_CMD_SOFT_RESET);
    if (rc != BM_OK) {
        return rc;
    }
    esp_rom_delay_us(10000u);  /* 10 ms，有界裸机延迟 */

    /* 配置加速度。 */
    rc = bm_vendor_bmi160_write_reg(handle, BM_VENDOR_BMI160_REG_ACC_CONF,
                                    cfg.acc_conf);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_vendor_bmi160_write_reg(handle, BM_VENDOR_BMI160_REG_ACC_RANGE,
                                    cfg.acc_range);
    if (rc != BM_OK) {
        return rc;
    }

    /* 配置陀螺仪。 */
    rc = bm_vendor_bmi160_write_reg(handle, BM_VENDOR_BMI160_REG_GYR_CONF,
                                    cfg.gyr_conf);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_vendor_bmi160_write_reg(handle, BM_VENDOR_BMI160_REG_GYR_RANGE,
                                    cfg.gyr_range);
    if (rc != BM_OK) {
        return rc;
    }

    /* 打开加速度计；BMI160 datasheet 要求转普通模式等待 ≥ 3.8 ms（取 10 ms）。 */
    rc = bm_vendor_bmi160_write_reg(handle, BM_VENDOR_BMI160_REG_CMD,
                                    BM_VENDOR_BMI160_CMD_ACC_NORMAL);
    if (rc != BM_OK) {
        return rc;
    }
    esp_rom_delay_us(10000u);  /* 10 ms，有界裸机延迟 */

    /* 打开陀螺仪；BMI160 datasheet 要求转普通模式等待 ≥ 80 ms（取 10 ms + 上层重试）。 */
    rc = bm_vendor_bmi160_write_reg(handle, BM_VENDOR_BMI160_REG_CMD,
                                    BM_VENDOR_BMI160_CMD_GYR_NORMAL);
    if (rc != BM_OK) {
        return rc;
    }
    esp_rom_delay_us(10000u);  /* 10 ms，有界裸机延迟 */

    handle->initialized = 1;
    return BM_OK;
}

/**
 * @brief 反初始化 BMI160。
 */
int bm_vendor_bmi160_deinit(bm_vendor_bmi160_handle_t *handle) {
    if (handle == NULL) {
        return BM_ERR_INVALID;
    }
    handle->initialized = 0;
    return BM_OK;
}

/**
 * @brief 读取 BMI160 原始数据。
 */
int bm_vendor_bmi160_read_raw(const bm_vendor_bmi160_handle_t *handle,
                              bm_vendor_bmi160_sample_t *sample) {
    uint8_t buf[BMI160_DATA_LEN];
    int rc;

    if (handle == NULL || sample == NULL) {
        return BM_ERR_INVALID;
    }
    if (handle->initialized == 0) {
        return BM_ERR_NOT_INIT;
    }

    memset(sample, 0, sizeof(*sample));

    rc = bm_vendor_bmi160_read_regs(handle, BM_VENDOR_BMI160_REG_DATA_START,
                                    buf, BMI160_DATA_LEN);
    if (rc != BM_OK) {
        return rc;
    }

    sample->gyro_raw[0]  = (int16_t)((uint16_t)buf[0]  | ((uint16_t)buf[1]  << 8u));
    sample->gyro_raw[1]  = (int16_t)((uint16_t)buf[2]  | ((uint16_t)buf[3]  << 8u));
    sample->gyro_raw[2]  = (int16_t)((uint16_t)buf[4]  | ((uint16_t)buf[5]  << 8u));
    sample->accel_raw[0] = (int16_t)((uint16_t)buf[6]  | ((uint16_t)buf[7]  << 8u));
    sample->accel_raw[1] = (int16_t)((uint16_t)buf[8]  | ((uint16_t)buf[9]  << 8u));
    sample->accel_raw[2] = (int16_t)((uint16_t)buf[10] | ((uint16_t)buf[11] << 8u));

    return BM_OK;
}

/**
 * @brief 获取当前配置视图。
 */
const bm_vendor_bmi160_config_t *bm_vendor_bmi160_get_config(
    const bm_vendor_bmi160_handle_t *handle) {
    if (handle == NULL || handle->initialized == 0) {
        return NULL;
    }
    return &handle->config;
}
