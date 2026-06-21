/**
 * @file bm_vendor_i2c_esp32_idf.h
 * @brief ESP32-WROOM-32E vendor 层共享硬件 I2C 接口（LL 寄存器级实现）
 *
 * 为 AS5600、BMI160 等 I2C 设备提供统一的 master 初始化和读写封装。
 * 本层使用 hal/i2c_ll.h LL API，有界忙等轮询完成位，
 * 零 FreeRTOS 依赖，零 IDF driver 层依赖。
 *
 * @author zeh (china_qzh@163.com)
 * @version 2.3
 * @date 2026-06-21
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-21       1.0            zeh           vendor 层共享硬件 I2C（IDF legacy 实现）
 * 2026-06-21       2.0            zeh         改为 LL 寄存器级裸机实现，零 RTOS 依赖
 * 2026-06-21       2.1            zeh         GPIO matrix 路由改用 esp_rom_gpio ROM 接口；
 *                                                加 bm_vendor_i2c_get_last_fail 诊断 getter
 * 2026-06-21       2.3            zeh         初始化顺序对齐官方 i2c_set_pin：先 GPIO 开漏高电平，
 *                                                再挂接 I2C matrix；补 bus-clear 恢复路径
 */
#ifndef BM_VENDOR_I2C_ESP32_IDF_H
#define BM_VENDOR_I2C_ESP32_IDF_H

#include "bm_types.h"

#include <stdint.h>
#include <stddef.h>

#include "hal/i2c_types.h"
#include "soc/gpio_num.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化指定 I2C 端口为 master 模式（LL 寄存器级，400 kHz）。
 *
 * 使能外设时钟、复位、配置 master 模式及 SCL/SDA 时序；引脚先配置为
 * GPIO 开漏高电平，再挂接 GPIO matrix，并在检测到 bus busy / 低电平时
 * 执行安全 bus-clear。同一端口可重复调用，已初始化时直接返回 BM_OK。
 *
 * @param port   I2C 端口号（I2C_NUM_0 或 I2C_NUM_1）。
 * @param sda    SDA GPIO 编号。
 * @param scl    SCL GPIO 编号。
 * @param clk_hz 总线时钟频率（Hz），通常 400000。
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法；BM_ERR_IO 硬件错误。
 */
int bm_vendor_i2c_port_init(i2c_port_t port, gpio_num_t sda,
                            gpio_num_t scl, uint32_t clk_hz);

/**
 * @brief 向 I2C 设备写入数据（有界忙等轮询，不调度 CPU）。
 *
 * @param port       I2C 端口号。
 * @param addr       7-bit 设备地址（不含 R/W 位）。
 * @param buf        写数据缓冲区（len > 0 时不得为 NULL）。
 * @param len        写入字节数。
 * @param timeout_ms 忙等预算（ms）；0 表示使用默认值 10 ms。
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法；BM_ERR_IO 超时或 NACK。
 */
int bm_vendor_i2c_write(i2c_port_t port, uint8_t addr,
                        const uint8_t *buf, size_t len,
                        uint32_t timeout_ms);

/**
 * @brief 向 I2C 设备写入后再读取（典型寄存器 read 流程，有界忙等轮询）。
 *
 * 事务序列：START→写地址(W)→写 write_buf→RESTART→写地址(R)→读 read_buf→STOP。
 *
 * @param port       I2C 端口号。
 * @param addr       7-bit 设备地址（不含 R/W 位）。
 * @param write_buf  写阶段数据缓冲区（write_len > 0 时不得为 NULL）。
 * @param write_len  写阶段字节数。
 * @param read_buf   读阶段数据缓冲区（read_len > 0 时不得为 NULL）。
 * @param read_len   读阶段字节数。
 * @param timeout_ms 忙等预算（ms）；0 表示使用默认值 10 ms。
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法；BM_ERR_IO 超时或 NACK。
 */
int bm_vendor_i2c_write_read(i2c_port_t port, uint8_t addr,
                             const uint8_t *write_buf, size_t write_len,
                             uint8_t *read_buf, size_t read_len,
                             uint32_t timeout_ms);

/**
 * @brief 获取指定端口最近一次 I2C 事务失败的诊断信息（临时诊断，确认根因后可删）。
 *
 * @param port   I2C 端口号（I2C_NUM_0 或 I2C_NUM_1）。
 * @param intr   输出参数，最近一次失败时的 int_status 寄存器值（NULL 则忽略）。
 * @param reason 输出参数，失败原因：0=ok, 1=NACK, 2=TIMEOUT, 3=ARB_LOST,
 *               4=POLL_TIMEOUT（轮询超时，总线可能未翻转）（NULL 则忽略）。
 */
void bm_vendor_i2c_get_last_fail(i2c_port_t port, uint32_t *intr, int *reason);

/**
 * @brief 获取指定端口最近一次 I2C 失败时的总线与引脚状态（临时诊断，确认根因后可删）。
 *
 * @param port       I2C 端口号（I2C_NUM_0 或 I2C_NUM_1）。
 * @param bus_busy   输出参数，最近一次失败时的 bus_busy 状态（NULL 则忽略）。
 * @param sda_level  输出参数，最近一次失败时的 SDA 线电平（NULL 则忽略）。
 * @param scl_level  输出参数，最近一次失败时的 SCL 线电平（NULL 则忽略）。
 * @param timeout_reg 输出参数，最近一次失败时的 timeout 寄存器值（NULL 则忽略）。
 */
void bm_vendor_i2c_get_last_fail_detail(i2c_port_t port,
                                        int *bus_busy,
                                        int *sda_level,
                                        int *scl_level,
                                        uint32_t *timeout_reg);

#ifdef __cplusplus
}
#endif

#endif /* BM_VENDOR_I2C_ESP32_IDF_H */
