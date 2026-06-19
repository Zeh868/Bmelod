/**
 * @file bm_vendor_encoder_esp32_idf.c
 * @brief ESP32-WROOM-32E 板级 AS5600 编码器裸机实现
 *
 * 两路编码器使用 GPIO 软 I2C 轮询访问，不依赖外设驱动层。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            新增编码器实现
 * 2026-06-19       1.1            zeh          改为 GPIO 软 I2C
 *
 */
#include "bm_vendor_encoder_esp32_idf.h"
#include "bm_hal_instances_esp32wroom32e.h"
#include "bm_types.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_rom_sys.h"
#include "hal/gpio_ll.h"

/** @brief 编码器实例数量。 */
#define BM_VENDOR_ENCODER_INSTANCE_COUNT  2u
/** @brief AS5600 设备地址。 */
#define BM_VENDOR_ENCODER_AS5600_ADDR     0x36u
/** @brief AS5600 RAW ANGLE 寄存器。 */
#define BM_VENDOR_ENCODER_AS5600_RAW_ANGLE_REG  0x0Cu
/** @brief 软 I2C 单次轮询上限。 */
#define BM_VENDOR_ENCODER_I2C_TIMEOUT_LOOPS  512u
/** @brief 软 I2C 半周期延迟（微秒）。 */
#define BM_VENDOR_ENCODER_I2C_HALF_PERIOD_US  2u

typedef struct {
    /** @brief 编号。 */
    uint32_t id;
} bm_vendor_encoder_config_t;

typedef struct {
    /** @brief 是否已完成引脚初始化。 */
    int initialized;
    /** @brief 最近一次读到的角度。 */
    int32_t last_angle;
} bm_vendor_encoder_context_t;

static bm_vendor_encoder_context_t g_encoder_context[BM_VENDOR_ENCODER_INSTANCE_COUNT];

static const uint32_t g_encoder_sda_gpio[BM_VENDOR_ENCODER_INSTANCE_COUNT] = {
    BM_ESP32WROOM32E_ENCODER0_SDA_GPIO,
    BM_ESP32WROOM32E_ENCODER1_SDA_GPIO,
};

static const uint32_t g_encoder_scl_gpio[BM_VENDOR_ENCODER_INSTANCE_COUNT] = {
    BM_ESP32WROOM32E_ENCODER0_SCL_GPIO,
    BM_ESP32WROOM32E_ENCODER1_SCL_GPIO,
};

/**
 * @brief 初始化单个编码器实例的引脚。
 */
static int bm_vendor_encoder_hw_init(bm_vendor_encoder_context_t *ctx,
                                     const bm_vendor_encoder_config_t *cfg);

/**
 * @brief 取出设备上下文。
 */
static bm_vendor_encoder_context_t *bm_vendor_encoder_context_for(const struct bm_hal_encoder *dev) {
    const bm_vendor_encoder_config_t *cfg;

    if (dev == NULL || dev->config == NULL) {
        return NULL;
    }
    cfg = (const bm_vendor_encoder_config_t *)dev->config;
    if (cfg->id >= BM_VENDOR_ENCODER_INSTANCE_COUNT) {
        return NULL;
    }
    return &g_encoder_context[cfg->id];
}

/**
 * @brief 将引脚配置为 I2C 软总线模式。
 */
static void bm_vendor_encoder_pin_init(uint32_t gpio_num) {
    gpio_ll_func_sel(&GPIO, (uint8_t)gpio_num, 2u);
    gpio_ll_output_enable(&GPIO, gpio_num);
    gpio_ll_input_enable(&GPIO, gpio_num);
    gpio_ll_od_enable(&GPIO, gpio_num);
    gpio_ll_set_level(&GPIO, gpio_num, 1u);
}

/**
 * @brief 释放总线并等待线路回到高电平。
 */
static int bm_vendor_encoder_bus_release(uint32_t sda_gpio, uint32_t scl_gpio) {
    uint32_t wait;

    gpio_ll_set_level(&GPIO, sda_gpio, 1u);
    gpio_ll_set_level(&GPIO, scl_gpio, 1u);
    for (wait = 0u; wait < BM_VENDOR_ENCODER_I2C_TIMEOUT_LOOPS; ++wait) {
        if (gpio_ll_get_level(&GPIO, sda_gpio) && gpio_ll_get_level(&GPIO, scl_gpio)) {
            return BM_OK;
        }
        esp_rom_delay_us(1u);
    }
    return BM_ERR_IO;
}

/**
 * @brief 生成软 I2C 起始条件。
 */
static int bm_vendor_encoder_i2c_start(uint32_t sda_gpio, uint32_t scl_gpio) {
    if (bm_vendor_encoder_bus_release(sda_gpio, scl_gpio) != BM_OK) {
        return BM_ERR_IO;
    }
    gpio_ll_set_level(&GPIO, sda_gpio, 0u);
    esp_rom_delay_us(BM_VENDOR_ENCODER_I2C_HALF_PERIOD_US);
    gpio_ll_set_level(&GPIO, scl_gpio, 0u);
    esp_rom_delay_us(BM_VENDOR_ENCODER_I2C_HALF_PERIOD_US);
    return BM_OK;
}

/**
 * @brief 生成软 I2C 停止条件。
 */
static void bm_vendor_encoder_i2c_stop(uint32_t sda_gpio, uint32_t scl_gpio) {
    gpio_ll_set_level(&GPIO, sda_gpio, 0u);
    esp_rom_delay_us(BM_VENDOR_ENCODER_I2C_HALF_PERIOD_US);
    gpio_ll_set_level(&GPIO, scl_gpio, 1u);
    esp_rom_delay_us(BM_VENDOR_ENCODER_I2C_HALF_PERIOD_US);
    gpio_ll_set_level(&GPIO, sda_gpio, 1u);
    esp_rom_delay_us(BM_VENDOR_ENCODER_I2C_HALF_PERIOD_US);
}

/**
 * @brief 写入一个比特。
 */
static void bm_vendor_encoder_i2c_write_bit(uint32_t sda_gpio,
                                           uint32_t scl_gpio,
                                           uint32_t bit) {
    gpio_ll_set_level(&GPIO, sda_gpio, bit ? 1u : 0u);
    esp_rom_delay_us(1u);
    gpio_ll_set_level(&GPIO, scl_gpio, 1u);
    esp_rom_delay_us(BM_VENDOR_ENCODER_I2C_HALF_PERIOD_US);
    gpio_ll_set_level(&GPIO, scl_gpio, 0u);
    esp_rom_delay_us(BM_VENDOR_ENCODER_I2C_HALF_PERIOD_US);
}

/**
 * @brief 读取一个比特。
 */
static uint32_t bm_vendor_encoder_i2c_read_bit(uint32_t sda_gpio, uint32_t scl_gpio) {
    uint32_t bit;

    gpio_ll_set_level(&GPIO, sda_gpio, 1u);
    esp_rom_delay_us(1u);
    gpio_ll_set_level(&GPIO, scl_gpio, 1u);
    esp_rom_delay_us(BM_VENDOR_ENCODER_I2C_HALF_PERIOD_US);
    bit = (uint32_t)gpio_ll_get_level(&GPIO, sda_gpio);
    gpio_ll_set_level(&GPIO, scl_gpio, 0u);
    esp_rom_delay_us(BM_VENDOR_ENCODER_I2C_HALF_PERIOD_US);
    return bit;
}

/**
 * @brief 写入一个字节并读取 ACK。
 */
static int bm_vendor_encoder_i2c_write_byte(uint32_t sda_gpio,
                                            uint32_t scl_gpio,
                                            uint8_t value) {
    uint32_t bit;
    uint32_t mask;

    for (mask = 0x80u; mask != 0u; mask >>= 1u) {
        bm_vendor_encoder_i2c_write_bit(sda_gpio, scl_gpio, (value & mask) ? 1u : 0u);
    }
    bit = bm_vendor_encoder_i2c_read_bit(sda_gpio, scl_gpio);
    return (bit == 0u) ? BM_OK : BM_ERR_IO;
}

/**
 * @brief 读取一个字节。
 */
static uint8_t bm_vendor_encoder_i2c_read_byte(uint32_t sda_gpio,
                                               uint32_t scl_gpio,
                                               int nack_after_read) {
    uint8_t value = 0u;
    uint32_t i;

    for (i = 0u; i < 8u; ++i) {
        value <<= 1u;
        value |= (uint8_t)bm_vendor_encoder_i2c_read_bit(sda_gpio, scl_gpio);
    }
    bm_vendor_encoder_i2c_write_bit(sda_gpio, scl_gpio, nack_after_read ? 1u : 0u);
    return value;
}

/**
 * @brief 读取 AS5600 RAW ANGLE。
 */
static int bm_vendor_encoder_read_raw_angle(const bm_vendor_encoder_config_t *cfg,
                                            int32_t *value) {
    uint32_t sda_gpio;
    uint32_t scl_gpio;
    uint8_t raw_hi;
    uint8_t raw_lo;

    if (cfg == NULL || value == NULL || cfg->id >= BM_VENDOR_ENCODER_INSTANCE_COUNT) {
        return BM_ERR_INVALID;
    }

    sda_gpio = g_encoder_sda_gpio[cfg->id];
    scl_gpio = g_encoder_scl_gpio[cfg->id];

    if (bm_vendor_encoder_i2c_start(sda_gpio, scl_gpio) != BM_OK) {
        return BM_ERR_IO;
    }
    if (bm_vendor_encoder_i2c_write_byte(sda_gpio, scl_gpio,
                                         (uint8_t)((BM_VENDOR_ENCODER_AS5600_ADDR << 1u) | 0u)) != BM_OK) {
        bm_vendor_encoder_i2c_stop(sda_gpio, scl_gpio);
        return BM_ERR_IO;
    }
    if (bm_vendor_encoder_i2c_write_byte(sda_gpio, scl_gpio,
                                         BM_VENDOR_ENCODER_AS5600_RAW_ANGLE_REG) != BM_OK) {
        bm_vendor_encoder_i2c_stop(sda_gpio, scl_gpio);
        return BM_ERR_IO;
    }
    if (bm_vendor_encoder_i2c_start(sda_gpio, scl_gpio) != BM_OK) {
        return BM_ERR_IO;
    }
    if (bm_vendor_encoder_i2c_write_byte(sda_gpio, scl_gpio,
                                         (uint8_t)((BM_VENDOR_ENCODER_AS5600_ADDR << 1u) | 1u)) != BM_OK) {
        bm_vendor_encoder_i2c_stop(sda_gpio, scl_gpio);
        return BM_ERR_IO;
    }

    raw_hi = bm_vendor_encoder_i2c_read_byte(sda_gpio, scl_gpio, 0);
    raw_lo = bm_vendor_encoder_i2c_read_byte(sda_gpio, scl_gpio, 1);
    bm_vendor_encoder_i2c_stop(sda_gpio, scl_gpio);

    *value = (int32_t)((((uint16_t)raw_hi << 8u) | raw_lo) & 0x0FFFu);
    return BM_OK;
}

/**
 * @brief 读取编码器数值。
 */
static int bm_vendor_encoder_read(const struct bm_hal_encoder *dev, int32_t *value) {
    bm_vendor_encoder_context_t *ctx;
    const bm_vendor_encoder_config_t *cfg;
    int rc;

    if (value == NULL) {
        return BM_ERR_INVALID;
    }
    ctx = bm_vendor_encoder_context_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    cfg = (const bm_vendor_encoder_config_t *)dev->config;
    rc = bm_vendor_encoder_hw_init(ctx, cfg);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_vendor_encoder_read_raw_angle(cfg, value);
    if (rc != BM_OK) {
        return rc;
    }
    ctx->last_angle = *value;
    return BM_OK;
}

/**
 * @brief 初始化所有软 I2C 引脚。
 */
static int bm_vendor_encoder_hw_init(bm_vendor_encoder_context_t *ctx,
                                     const bm_vendor_encoder_config_t *cfg) {
    uint32_t sda_gpio;
    uint32_t scl_gpio;

    if (ctx == NULL || cfg == NULL || cfg->id >= BM_VENDOR_ENCODER_INSTANCE_COUNT) {
        return BM_ERR_INVALID;
    }
    if (ctx->initialized != 0) {
        return BM_OK;
    }

    sda_gpio = g_encoder_sda_gpio[cfg->id];
    scl_gpio = g_encoder_scl_gpio[cfg->id];
    bm_vendor_encoder_pin_init(sda_gpio);
    bm_vendor_encoder_pin_init(scl_gpio);
    if (bm_vendor_encoder_bus_release(sda_gpio, scl_gpio) != BM_OK) {
        return BM_ERR_IO;
    }
    ctx->initialized = 1;
    return BM_OK;
}

static const struct bm_encoder_driver_api g_encoder_api = {
    bm_vendor_encoder_read,
};

static const bm_vendor_encoder_config_t g_encoder_config_m0 = {
    0u,
};

static const bm_vendor_encoder_config_t g_encoder_config_m1 = {
    1u,
};

const bm_hal_encoder_t bm_hal_encoder_m0 = { &g_encoder_api, &g_encoder_config_m0 };
const bm_hal_encoder_t bm_hal_encoder_m1 = { &g_encoder_api, &g_encoder_config_m1 };
