/**
 * @file bm_vendor_encoder_esp32_idf.c
 * @brief ESP32-WROOM-32E 板级 AS5600 编码器硬件 I2C 实现（LL 裸机）
 *
 * 两路编码器通过 bm_vendor_i2c_esp32_idf LL 接口读取 AS5600 RAW ANGLE：
 * - M0：I2C_NUM_1，SDA=GPIO19，SCL=GPIO18（与 BMI160 共用）
 * - M1：I2C_NUM_0，SDA=GPIO23，SCL=GPIO5
 *
 * 总线速率：M0（I2C1）400 kHz（fast mode，与 BMI160 共线一致），M1（I2C0）
 * 100 kHz。I2C 端口初始化由 bmi160_init（M0）或 encoder 首次 read 前调用
 * bm_vendor_i2c_port_init 完成（幂等）；read 在端口未初始化时执行一次幂等
 * 懒初始化兜底，兼容直接调用 read 的上层。
 *
 * @author zeh (china_qzh@163.com)
 * @version 2.4
 * @date 2026-06-21
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            新增编码器实现
 * 2026-06-19       1.1            zeh            改为 GPIO 软 I2C
 * 2026-06-21       2.0            zeh           改用 ESP-IDF 硬件 I2C 400kHz
 * 2026-06-21       2.1            zeh         改为 LL 裸机接口，移除 read 懒初始化
 * 2026-06-21       2.2            zeh         M0/M1 均使用 100kHz，提高电机出波期间
 *                                                的 I2C 抗干扰余量
 * 2026-06-21       2.3            zeh         更正文件头：read 保留幂等懒初始化兜底、
 *                                                总线速率改写为 100 kHz，与代码一致
 * 2026-06-21       2.4            zeh         M0 I2C 提速至 400kHz（共享 I2C1，与
 *                                                BMI160 一致），M1 维持 100kHz
 */
#include "bm_vendor_encoder_esp32_idf.h"
#include "bm_vendor_i2c_esp32_idf.h"
#include "bm_vendor_esp32_idf_compat.h"
#include "bm_hal_instances_esp32wroom32e.h"
#include "bm_types.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/** @brief 编码器实例数量。 */
#define BM_VENDOR_ENCODER_INSTANCE_COUNT  2u
/** @brief AS5600 设备地址。 */
#define BM_VENDOR_ENCODER_AS5600_ADDR     0x36u
/** @brief AS5600 RAW ANGLE 寄存器。 */
#define BM_VENDOR_ENCODER_AS5600_RAW_ANGLE_REG  0x0Cu
/**
 * @brief M0 AS5600 硬件 I2C 时钟频率（Hz）。
 *
 * M0 与 BMI160 共用 I2C1。底层 I2C 已加入 7 周期 SCL/SDA glitch 滤波并经真机
 * 验证：电机出波期间 400 kHz（fast mode）已稳定，不再出现地址 NACK、SDA 卡低
 * 与控制器 busy，因此 M0 恢复 fast mode 400 kHz。M0 与 BMI160 共用 I2C1，二者
 * 速率统一为 400 kHz，消除初始化顺序定速带来的共线速率不一致。
 */
#define BM_VENDOR_ENCODER_M0_I2C_CLK_HZ   400000u
/**
 * @brief M1 AS5600 硬件 I2C 时钟频率（Hz）。
 *
 * M1 使用 GPIO23/GPIO5，当前仅依赖内部弱上拉；在 400 kHz 下 SCL 上升沿更慢，
 * 更容易触发 ESP32 I2C0 的硬件 timeout 窗口，因此单独降到 100 kHz。
 */
#define BM_VENDOR_ENCODER_M1_I2C_CLK_HZ   100000u

typedef struct {
    /** @brief 编号。 */
    uint32_t id;
    /** @brief I2C 端口号。 */
    i2c_port_t i2c_port;
    /** @brief SDA GPIO。 */
    gpio_num_t sda_gpio;
    /** @brief SCL GPIO。 */
    gpio_num_t scl_gpio;
} bm_vendor_encoder_config_t;

typedef struct {
    /** @brief 是否已完成 I2C 初始化。 */
    int initialized;
    /** @brief 最近一次读到的角度。 */
    int32_t last_angle;
} bm_vendor_encoder_context_t;

static bm_vendor_encoder_context_t g_encoder_context[BM_VENDOR_ENCODER_INSTANCE_COUNT];

static const bm_vendor_encoder_config_t g_encoder_config_m0 = {
    0u,
    I2C_NUM_1,
    (gpio_num_t)BM_ESP32WROOM32E_ENCODER0_SDA_GPIO,  /* GPIO19 */
    (gpio_num_t)BM_ESP32WROOM32E_ENCODER0_SCL_GPIO,  /* GPIO18 */
};

static const bm_vendor_encoder_config_t g_encoder_config_m1 = {
    1u,
    I2C_NUM_0,
    (gpio_num_t)BM_ESP32WROOM32E_ENCODER1_SDA_GPIO,  /* GPIO23 */
    (gpio_num_t)BM_ESP32WROOM32E_ENCODER1_SCL_GPIO,  /* GPIO5 */
};

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
 * @brief 初始化单路编码器的 I2C 端口（幂等，可被上层或 bmi160_init 提前调用）。
 *
 * @param dev HAL 设备实例。
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法；BM_ERR_IO 硬件错误。
 */
static int bm_vendor_encoder_port_init(const struct bm_hal_encoder *dev)
{
    bm_vendor_encoder_context_t *ctx;
    const bm_vendor_encoder_config_t *cfg;
    int rc;

    ctx = bm_vendor_encoder_context_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    if (ctx->initialized != 0) {
        return BM_OK;
    }
    cfg = (const bm_vendor_encoder_config_t *)dev->config;
    rc = bm_vendor_i2c_port_init(cfg->i2c_port, cfg->sda_gpio,
                                  cfg->scl_gpio,
                                  (cfg->id == 0u)
                                      ? BM_VENDOR_ENCODER_M0_I2C_CLK_HZ
                                      : BM_VENDOR_ENCODER_M1_I2C_CLK_HZ);
    if (rc == BM_OK) {
        ctx->initialized = 1;
    }
    return rc;
}

/**
 * @brief 读取编码器数值（有界事务，不做懒初始化）。
 *
 * 调用前须保证对应 I2C 端口已通过 bm_vendor_encoder_port_init 或
 * bm_vendor_bmi160_init（M0 共用总线）初始化；否则返回 BM_ERR_IO。
 */
static int bm_vendor_encoder_read(const struct bm_hal_encoder *dev, int32_t *value) {
    bm_vendor_encoder_context_t *ctx;
    const bm_vendor_encoder_config_t *cfg;
    uint8_t reg = BM_VENDOR_ENCODER_AS5600_RAW_ANGLE_REG;
    uint8_t buf[2];
    int rc;

    if (value == NULL) {
        return BM_ERR_INVALID;
    }
    ctx = bm_vendor_encoder_context_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    cfg = (const bm_vendor_encoder_config_t *)dev->config;

    /* 端口未初始化时执行一次幂等初始化（兼容直接调用 read 的上层） */
    if (ctx->initialized == 0) {
        rc = bm_vendor_encoder_port_init(dev);
        if (rc != BM_OK) {
            return rc;
        }
    }

    rc = bm_vendor_i2c_write_read(cfg->i2c_port,
                                  BM_VENDOR_ENCODER_AS5600_ADDR,
                                  &reg, 1u,
                                  buf, 2u,
                                  0u);
    if (rc != BM_OK) {
        return rc;
    }

    *value = (int32_t)((((uint16_t)buf[0] << 8u) | (uint16_t)buf[1]) & 0x0FFFu);
    ctx->last_angle = *value;
    return BM_OK;
}

static const struct bm_encoder_driver_api g_encoder_api = {
    bm_vendor_encoder_read,
};

const bm_hal_encoder_t bm_hal_encoder_m0 = { &g_encoder_api, &g_encoder_config_m0 };
const bm_hal_encoder_t bm_hal_encoder_m1 = { &g_encoder_api, &g_encoder_config_m1 };
