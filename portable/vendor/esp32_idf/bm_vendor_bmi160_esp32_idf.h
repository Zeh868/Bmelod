/**
 * @file bm_vendor_bmi160_esp32_idf.h
 * @brief ESP32-WROOM-32E vendor 专用 BMI160 驱动接口
 *
 * 该接口提供 BMI160 加速度计/陀螺仪的原始采样读取能力，支持 I2C 与 SPI
 * 两种主接口配置。当前仓库的原理图文本抽取未能直接暴露 BMI160 的板级
 * 连线，因此本接口采用“显式配置 + 透明采样”的方式，不臆造任何 GPIO。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            新增 BMI160 vendor 专用驱动接口
 *
 */
#ifndef BM_VENDOR_BMI160_ESP32_IDF_H
#define BM_VENDOR_BMI160_ESP32_IDF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bm_types.h"

/**
 * @brief BMI160 默认 I2C 地址（SDO/SA0 接 GND）。
 */
#define BM_VENDOR_BMI160_I2C_ADDR_DEFAULT  0x68u
/**
 * @brief BMI160 备用 I2C 地址（SDO/SA0 接 VDDIO）。
 */
#define BM_VENDOR_BMI160_I2C_ADDR_ALT      0x69u

/** @brief BMI160 芯片 ID。 */
#define BM_VENDOR_BMI160_CHIP_ID           0xD1u
/** @brief BMI160 命令寄存器。 */
#define BM_VENDOR_BMI160_REG_CMD           0x7Eu
/** @brief BMI160 芯片 ID 寄存器。 */
#define BM_VENDOR_BMI160_REG_CHIP_ID       0x00u
/** @brief BMI160 状态寄存器。 */
#define BM_VENDOR_BMI160_REG_STATUS        0x1Bu
/** @brief BMI160 加速度配置寄存器。 */
#define BM_VENDOR_BMI160_REG_ACC_CONF      0x40u
/** @brief BMI160 加速度量程寄存器。 */
#define BM_VENDOR_BMI160_REG_ACC_RANGE     0x41u
/** @brief BMI160 陀螺仪配置寄存器。 */
#define BM_VENDOR_BMI160_REG_GYR_CONF      0x42u
/** @brief BMI160 陀螺仪量程寄存器。 */
#define BM_VENDOR_BMI160_REG_GYR_RANGE     0x43u
/** @brief BMI160 原始数据起始寄存器（gyro_x_lsb）。 */
#define BM_VENDOR_BMI160_REG_DATA_START    0x0Cu
/** @brief BMI160 原始数据连续读取长度（gyro + accel）。 */
#define BM_VENDOR_BMI160_DATA_BURST_LEN    12u

/** @brief BMI160 软复位命令。 */
#define BM_VENDOR_BMI160_CMD_SOFT_RESET    0xB6u
/** @brief BMI160 加速度 normal mode 命令。 */
#define BM_VENDOR_BMI160_CMD_ACC_NORMAL    0x11u
/** @brief BMI160 陀螺仪 normal mode 命令。 */
#define BM_VENDOR_BMI160_CMD_GYR_NORMAL    0x15u

/** @brief BMI160 默认加速度配置（100Hz，表内 reset 值）。 */
#define BM_VENDOR_BMI160_DEFAULT_ACC_CONF  0x28u
/** @brief BMI160 默认加速度量程（±4g）。 */
#define BM_VENDOR_BMI160_DEFAULT_ACC_RANGE  0x05u
/** @brief BMI160 默认陀螺仪配置（100Hz，表内 reset 值）。 */
#define BM_VENDOR_BMI160_DEFAULT_GYR_CONF  0x28u
/** @brief BMI160 默认陀螺仪量程（±1000°/s）。 */
#define BM_VENDOR_BMI160_DEFAULT_GYR_RANGE  0x01u

/**
 * @brief BMI160 主接口类型。
 */
typedef enum bm_vendor_bmi160_bus_type {
    /** @brief I2C 主接口。 */
    BM_VENDOR_BMI160_BUS_I2C = 0,
    /** @brief SPI 主接口。 */
    BM_VENDOR_BMI160_BUS_SPI = 1,
} bm_vendor_bmi160_bus_type_t;

/**
 * @brief BMI160 板级/应用侧显式配置。
 *
 * 对于 I2C，使用 bus_id 作为 I2C 控制器号，sda_gpio/scl_gpio/address 生效；
 * 对于 SPI，使用 bus_id 作为 SPI host，cs_gpio/mosi_gpio/miso_gpio/sck_gpio 生效。
 * int1_gpio/int2_gpio 仅作为保留字段，不参与当前轮询读取路径。
 */
typedef struct bm_vendor_bmi160_config {
    /** @brief 主接口类型。 */
    bm_vendor_bmi160_bus_type_t bus_type;
    /** @brief 总线编号：I2C port 或 SPI host。 */
    uint32_t bus_id;
    /** @brief 总线时钟（Hz）。I2C 默认建议 400k，SPI 默认建议 1MHz。 */
    uint32_t clock_hz;
    /** @brief I2C SDA 引脚（仅 I2C 生效）。 */
    int sda_gpio;
    /** @brief I2C SCL 引脚（仅 I2C 生效）。 */
    int scl_gpio;
    /** @brief SPI CS 引脚（仅 SPI 生效）。 */
    int cs_gpio;
    /** @brief SPI MOSI 引脚（仅 SPI 生效）。 */
    int mosi_gpio;
    /** @brief SPI MISO 引脚（仅 SPI 生效）。 */
    int miso_gpio;
    /** @brief SPI SCK 引脚（仅 SPI 生效）。 */
    int sck_gpio;
    /** @brief I2C 地址。未指定时可使用 BM_VENDOR_BMI160_I2C_ADDR_DEFAULT。 */
    uint8_t address;
    /** @brief SPI 是否使用 3-wire。当前实现仅支持 4-wire 读取；设为 true 会返回不支持。 */
    bool three_wire;
    /** @brief INT1 引脚编号；负值表示未连接/不使用。 */
    int int1_gpio;
    /** @brief INT2 引脚编号；负值表示未连接/不使用。 */
    int int2_gpio;
    /** @brief ACC_CONF 原始配置值。0 表示使用默认值。 */
    uint8_t acc_conf;
    /** @brief ACC_RANGE 原始配置值。0 表示使用默认值。 */
    uint8_t acc_range;
    /** @brief GYR_CONF 原始配置值。0 表示使用默认值。 */
    uint8_t gyr_conf;
    /** @brief GYR_RANGE 原始配置值。0 表示使用默认值。 */
    uint8_t gyr_range;
} bm_vendor_bmi160_config_t;

/**
 * @brief BMI160 原始采样块。
 */
typedef struct bm_vendor_bmi160_sample {
    /** @brief 三轴陀螺仪原始值，按 x/y/z 排列。 */
    int16_t gyro_raw[3];
    /** @brief 三轴加速度原始值，按 x/y/z 排列。 */
    int16_t accel_raw[3];
    /** @brief 温度原始值；当前实现默认仅在显式读取时更新。 */
    int16_t temperature_raw;
    /** @brief 状态寄存器缓存。 */
    uint8_t status;
} bm_vendor_bmi160_sample_t;

typedef struct bm_vendor_bmi160_handle bm_vendor_bmi160_handle_t;

/**
 * @brief 初始化 BMI160 设备句柄。
 *
 * @param handle 句柄实例（由调用者提供存储）。
 * @param config 显式板级/应用侧配置。
 * @return BM_OK 成功；否则返回 BM_ERR_*。
 */
int bm_vendor_bmi160_init(bm_vendor_bmi160_handle_t *handle,
                          const bm_vendor_bmi160_config_t *config);

/**
 * @brief 反初始化 BMI160 设备句柄。
 *
 * 该接口仅释放驱动侧持有的总线资源句柄；若总线为共享总线，不会删除底层总线。
 *
 * @param handle 设备句柄。
 * @return BM_OK 成功；否则返回 BM_ERR_*。
 */
int bm_vendor_bmi160_deinit(bm_vendor_bmi160_handle_t *handle);

/**
 * @brief 读取一帧 BMI160 原始采样。
 *
 * @param handle 设备句柄。
 * @param sample 输出采样块。
 * @return BM_OK 成功；否则返回 BM_ERR_*。
 */
int bm_vendor_bmi160_read_raw(const bm_vendor_bmi160_handle_t *handle,
                              bm_vendor_bmi160_sample_t *sample);

/**
 * @brief 获取句柄当前配置的只读视图。
 *
 * @param handle 设备句柄。
 * @return 配置指针；非法时返回 NULL。
 */
const bm_vendor_bmi160_config_t *bm_vendor_bmi160_get_config(
    const bm_vendor_bmi160_handle_t *handle);

#endif /* BM_VENDOR_BMI160_ESP32_IDF_H */
