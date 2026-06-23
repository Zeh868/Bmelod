/**
 * @file bm_vendor_i2c_esp32_idf.c
 * @brief ESP32-WROOM-32E vendor 层共享硬件 I2C 实现（LL 寄存器级）
 *
 * 使用 hal/i2c_ll.h LL API 直接配置 I2C 外设寄存器，有界忙等轮询
 * 命令完成位，不安装 IDF driver 层，不依赖 FreeRTOS 调度器。
 * 适用于 AS5600（400 kHz）与 BMI160（400 kHz）共用总线场景。
 *
 * 命令链设计（ESP32 I2C 命令寄存器状态机）：
 *   写事务：START → WRITE(addr+W, data...) → STOP → END
 *   写读事务：START → WRITE(addr+W, reg...) → RESTART →
 *             WRITE(addr+R) → READ(N-1, ACK) → READ(1, NACK) → STOP → END
 *
 * @note ESP32 I2C FIFO 深度 32 字节（SOC_I2C_FIFO_LEN），单次事务最大
 *       读/写各 31 字节（减去地址字节）。AS5600 读 2 字节、BMI160 读
 *       12 字节均在限制内。
 *
 * @author zeh (china_qzh@163.com)
 * @version 2.4
 * @date 2026-06-21
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-21       1.0            zeh           vendor 层共享硬件 I2C（IDF legacy 实现）
 * 2026-06-21       2.0            zeh         改为 LL 寄存器级裸机实现，零 RTOS 依赖
 * 2026-06-21       2.1            zeh         GPIO matrix 路由改用 esp_rom_gpio ROM 接口
 *                                                （对齐 esp-idf i2c_set_pin，oen_sel=0）；
 *                                                poll_done 按端口记录 last-fail 诊断信息
 * 2026-06-21       2.2            zeh         参考官方 I2C master 的 bus busy / timeout
 *                                                恢复路径，补齐端口级失败诊断
 * 2026-06-21       2.3            zeh         对齐官方 i2c_set_pin 顺序：先 GPIO 开漏高电平，
 *                                                再挂接 I2C matrix；补静态 bus-clear 恢复路径
 * 2026-06-21       2.4            zeh         开启 7 周期 glitch 滤波修复电机噪声导致的 NACK；
 *                                                移除 vendor 层 printf 恢复确定性流式特性
 * 2026-06-21       2.5            zeh         整理：提取 prepare_bus 助手去重 write/write_read 的
 *                                                总线准备段；移除临时失败诊断脚手架与两个 getter
 */
#include "bm_vendor_i2c_esp32_idf.h"
#include "bm_vendor_esp32_idf_compat.h"

#include <string.h>
#include <stdint.h>
#include <stddef.h>

/* LL / SoC 头文件（无 driver/i2c.h，无 FreeRTOS）*/
#include "hal/i2c_ll.h"
#include "hal/i2c_types.h"
#include "soc/gpio_num.h"
#include "soc/gpio_sig_map.h"
#include "hal/gpio_ll.h"
#include "esp_private/periph_ctrl.h"
#include "esp_rom_sys.h"
#include "esp_rom_gpio.h"

/* ---------- 私有常量 ---------- */

/** @brief 默认忙等预算（毫秒）。 */
#define BM_VENDOR_I2C_DEFAULT_TIMEOUT_MS  10u

/**
 * @brief ESP32 APB 时钟频率（Hz）。
 *
 * ESP32 classic APB 固定为 80 MHz（不随 CPU 频率变化）。
 * I2C 时序寄存器的单位为 APB cycle。
 */
#define BM_VENDOR_I2C_APB_CLK_HZ         80000000u

/**
 * @brief 忙等轮询单步延迟（µs）。
 *
 * 每次轮询等 1 µs；timeout_ms 转换为 µs 后作为上限计数。
 */
#define BM_VENDOR_I2C_POLL_STEP_US        1u

/**
 * @brief STOP 完成后等待控制器释放 bus_busy 的最大时间（µs）。
 */
#define BM_VENDOR_I2C_IDLE_WAIT_US         100u

/**
 * @brief GPIO bus-clear 半周期延迟（µs）。
 */
#define BM_VENDOR_I2C_BUS_CLEAR_HALF_US    5u

/**
 * @brief GPIO bus-clear 释放稳定延迟（µs）。
 */
#define BM_VENDOR_I2C_BUS_CLEAR_SETTLE_US   5u

/**
 * @brief 手动 bus-clear 的最大脉冲数。
 */
#define BM_VENDOR_I2C_BUS_CLEAR_PULSES      9u

/**
 * @brief 最大支持端口数（ESP32 有 I2C0 / I2C1）。
 */
#define BM_VENDOR_I2C_PORT_MAX            2u

/**
 * @brief 各 I2C 端口的 GPIO matrix 输出信号编号（SDA/SCL → GPIO）。
 *
 * 来自 soc/gpio_sig_map.h 的宏常量（避免运行时查表，零链接依赖）：
 *   I2C0: SDA_OUT=I2CEXT0_SDA_OUT_IDX(30), SCL_OUT=I2CEXT0_SCL_OUT_IDX(29)
 *   I2C1: SDA_OUT=I2CEXT1_SDA_OUT_IDX(96), SCL_OUT=I2CEXT1_SCL_OUT_IDX(95)
 */
static const int g_i2c_sda_out_sig[BM_VENDOR_I2C_PORT_MAX] = {
    I2CEXT0_SDA_OUT_IDX,
    I2CEXT1_SDA_OUT_IDX,
};
static const int g_i2c_sda_in_sig[BM_VENDOR_I2C_PORT_MAX] = {
    I2CEXT0_SDA_IN_IDX,
    I2CEXT1_SDA_IN_IDX,
};
static const int g_i2c_scl_out_sig[BM_VENDOR_I2C_PORT_MAX] = {
    I2CEXT0_SCL_OUT_IDX,
    I2CEXT1_SCL_OUT_IDX,
};
static const int g_i2c_scl_in_sig[BM_VENDOR_I2C_PORT_MAX] = {
    I2CEXT0_SCL_IN_IDX,
    I2CEXT1_SCL_IN_IDX,
};

/* ---------- 初始化状态 ---------- */

/** @brief 各端口初始化完成标志。 */
static int g_i2c_initialized[BM_VENDOR_I2C_PORT_MAX] = { 0, 0 };

/**
 * @brief 各端口已配置的 SDA GPIO。
 *
 * 用于故障诊断与恢复路径的线电平采样。
 */
static gpio_num_t g_i2c_sda_gpio[BM_VENDOR_I2C_PORT_MAX] = {
    GPIO_NUM_NC,
    GPIO_NUM_NC,
};

/**
 * @brief 各端口已配置的 SCL GPIO。
 *
 * 用于故障诊断与恢复路径的线电平采样。
 */
static gpio_num_t g_i2c_scl_gpio[BM_VENDOR_I2C_PORT_MAX] = {
    GPIO_NUM_NC,
    GPIO_NUM_NC,
};

/**
 * @brief 各端口最近一次配置的 I2C 速率。
 *
 * 用于硬件恢复后重建 timing。
 */
static uint32_t g_i2c_clk_hz[BM_VENDOR_I2C_PORT_MAX] = { 0u, 0u };

/* ---------- 静态辅助函数 ---------- */

/**
 * @brief 配置单个 GPIO 为开漏 GPIO 输出，并断开外设矩阵。
 *
 * 该路径用于端口初始化前的安全拉高，以及 bus-clear 期间的人工 SCL/SDA
 * 驱动。输出临时接到 SIG_GPIO_OUT_IDX，只有 GPIO 驱动能控制电平。
 *
 * @param gpio_num  GPIO 编号。
 */
static void bm_vendor_i2c_gpio_route_gpio(gpio_num_t gpio_num)
{
    /* 先把 GPIO 输出寄存器置高，避免切换功能瞬间产生低电平毛刺 */
    gpio_ll_set_level(&GPIO, gpio_num, 1u);

    /* 选择 GPIO matrix 功能（func=2 在 ESP32 上为 GPIO matrix 路由） */
    gpio_ll_func_sel(&GPIO, (uint8_t)gpio_num, 2u);

    /* 开漏输出，允许 I2C 从设备拉低 */
    gpio_ll_od_enable(&GPIO, gpio_num);
    gpio_ll_output_enable(&GPIO, gpio_num);
    gpio_ll_input_enable(&GPIO, gpio_num);

    /* 使能内部上拉（外部通常已有 4.7k，内部 ~45k 作为备份） */
    gpio_ll_pulldown_dis(&GPIO, gpio_num);
    gpio_ll_pullup_en(&GPIO, gpio_num);

    /* 断开外设输出，只有 GPIO 驱动能控制电平。 */
    esp_rom_gpio_connect_out_signal((uint32_t)gpio_num, (uint32_t)SIG_GPIO_OUT_IDX,
                                    false, false);
}

/**
 * @brief 将单个 GPIO 按 I2C 方式接回 GPIO matrix。
 *
 * @param gpio_num  GPIO 编号。
 * @param out_sig   GPIO matrix 输出信号编号（I2C 外设→GPIO）。
 * @param in_sig    GPIO matrix 输入信号编号（GPIO→I2C 外设）。
 */
static void bm_vendor_i2c_gpio_route_i2c(gpio_num_t gpio_num, int out_sig, int in_sig)
{
    /* 先把 GPIO 输出寄存器置高，避免切换功能瞬间产生低电平毛刺 */
    gpio_ll_set_level(&GPIO, gpio_num, 1u);

    /* 选择 GPIO matrix 功能（func=2 在 ESP32 上为 GPIO matrix 路由） */
    gpio_ll_func_sel(&GPIO, (uint8_t)gpio_num, 2u);

    /* 开漏输出，允许 I2C 从设备拉低 */
    gpio_ll_od_enable(&GPIO, gpio_num);
    gpio_ll_output_enable(&GPIO, gpio_num);
    gpio_ll_input_enable(&GPIO, gpio_num);

    /* 使能内部上拉（外部通常已有 4.7k，内部 ~45k 作为备份） */
    gpio_ll_pulldown_dis(&GPIO, gpio_num);
    gpio_ll_pullup_en(&GPIO, gpio_num);

    /* GPIO matrix 路由：使用 ROM 常驻接口（与 esp-idf i2c_set_pin 等价，oen_inv=false）。
     * 对齐参考：components/driver/i2c/i2c.c i2c_set_pin() 中：
     *   esp_rom_gpio_connect_out_signal(gpio, sig, 0, 0);
     *   esp_rom_gpio_connect_in_signal(gpio, sig, 0);
     * oen_inv=false 表示输出使能不反转，I2C 外设可直接控制总线拉低。
     * 无 FreeRTOS 依赖，ROM 驻留，符合 vendor 裸机契约。 */
    esp_rom_gpio_connect_out_signal((uint32_t)gpio_num, (uint32_t)out_sig, false, false);
    esp_rom_gpio_connect_in_signal((uint32_t)gpio_num, (uint32_t)in_sig,  false);
}

/**
 * @brief 安全执行 I2C bus-clear。
 *
 * 过程：
 * 1) 临时将 SDA/SCL 从 I2C matrix 切到 GPIO 输出；
 * 2) 先释放两线为高电平，确认 SCL 未被外部拉低；
 * 3) 若 SDA 仍低，则输出最多 9 个 SCL 脉冲；
 * 4) 生成 STOP：SDA 低 -> SCL 高 -> SDA 高；
 * 5) 重新挂回 I2C matrix，并复查电平。
 *
 * @param p  端口索引。
 * @return BM_OK 成功；BM_ERR_IO 物理总线仍异常。
 */
static int bm_vendor_i2c_bus_clear(int p)
{
    gpio_num_t sda_gpio;
    gpio_num_t scl_gpio;
    int        sda_level;
    int        scl_level;
    uint32_t   pulse;

    if (p < 0 || p >= (int)BM_VENDOR_I2C_PORT_MAX) {
        return BM_ERR_INVALID;
    }

    sda_gpio = g_i2c_sda_gpio[p];
    scl_gpio = g_i2c_scl_gpio[p];
    if (sda_gpio == GPIO_NUM_NC || scl_gpio == GPIO_NUM_NC) {
        return BM_ERR_IO;
    }

    bm_vendor_i2c_gpio_route_gpio(sda_gpio);
    bm_vendor_i2c_gpio_route_gpio(scl_gpio);

    /* 先释放两线，给外部上拉一个稳定窗口。 */
    gpio_ll_set_level(&GPIO, sda_gpio, 1u);
    gpio_ll_set_level(&GPIO, scl_gpio, 1u);
    esp_rom_delay_us(BM_VENDOR_I2C_BUS_CLEAR_SETTLE_US);

    sda_level = gpio_ll_get_level(&GPIO, (uint32_t)sda_gpio);
    scl_level = gpio_ll_get_level(&GPIO, (uint32_t)scl_gpio);

    /* SCL 被外部拉低时不能继续强行脉冲。 */
    if (scl_level == 0) {
        goto restore_and_fail;
    }

    if (sda_level == 0) {
        for (pulse = 0u; pulse < BM_VENDOR_I2C_BUS_CLEAR_PULSES; ++pulse) {
            gpio_ll_set_level(&GPIO, scl_gpio, 0u);
            esp_rom_delay_us(BM_VENDOR_I2C_BUS_CLEAR_HALF_US);

            gpio_ll_set_level(&GPIO, scl_gpio, 1u);
            esp_rom_delay_us(BM_VENDOR_I2C_BUS_CLEAR_HALF_US);

            scl_level = gpio_ll_get_level(&GPIO, (uint32_t)scl_gpio);
            if (scl_level == 0) {
                goto restore_and_fail;
            }

            sda_level = gpio_ll_get_level(&GPIO, (uint32_t)sda_gpio);
            if (sda_level != 0) {
                break;
            }
        }
    }

    /* 生成 STOP：SDA 低 -> SCL 高 -> SDA 高。 */
    gpio_ll_set_level(&GPIO, sda_gpio, 0u);
    esp_rom_delay_us(BM_VENDOR_I2C_BUS_CLEAR_HALF_US);

    gpio_ll_set_level(&GPIO, scl_gpio, 1u);
    esp_rom_delay_us(BM_VENDOR_I2C_BUS_CLEAR_HALF_US);
    scl_level = gpio_ll_get_level(&GPIO, (uint32_t)scl_gpio);
    if (scl_level == 0) {
        goto restore_and_fail;
    }

    gpio_ll_set_level(&GPIO, sda_gpio, 1u);
    esp_rom_delay_us(BM_VENDOR_I2C_BUS_CLEAR_SETTLE_US);

restore_and_fail:
    bm_vendor_i2c_gpio_route_i2c(sda_gpio,
                                 g_i2c_sda_out_sig[p],
                                 g_i2c_sda_in_sig[p]);
    bm_vendor_i2c_gpio_route_i2c(scl_gpio,
                                 g_i2c_scl_out_sig[p],
                                 g_i2c_scl_in_sig[p]);

    sda_level = gpio_ll_get_level(&GPIO, (uint32_t)sda_gpio);
    scl_level = gpio_ll_get_level(&GPIO, (uint32_t)scl_gpio);

    if (sda_level == 0 || scl_level == 0) {
        return BM_ERR_IO;
    }
    return BM_OK;
}

/**
 * @brief 有界等待 I2C 控制器进入总线空闲态。
 *
 * ESP32 的 TRANS_COMPLETE 标志可能早于 STOP 条件完全释放 bus_busy。
 * 调用方必须等待 bus_busy 清零后，才能开始下一笔事务。
 *
 * @param hw        I2C 设备寄存器。
 * @param budget_us 最大等待时间（µs）。
 * @return true 已进入空闲态；false 等待超时。
 */
static bool bm_vendor_i2c_wait_bus_idle(i2c_dev_t *hw, uint32_t budget_us)
{
    uint32_t elapsed;

    for (elapsed = 0u; elapsed < budget_us; elapsed += BM_VENDOR_I2C_POLL_STEP_US) {
        if (!i2c_ll_is_bus_busy(hw)) {
            return true;
        }
        esp_rom_delay_us(BM_VENDOR_I2C_POLL_STEP_US);
    }

    return !i2c_ll_is_bus_busy(hw);
}

/**
 * @brief 依据已保存的端口配置，恢复 I2C 硬件状态机与时序寄存器。
 *
 * 恢复步骤：
 * 1) 先做静态 bus-clear，必要时释放被目标设备卡住的 SDA；
 * 2) 再重置并重建 I2C 外设状态机与时序寄存器；
 * 3) 复查总线是否仍 busy / 仍为低电平。
 *
 * 该路径对齐官方 driver 在检测到 bus busy / timeout 后的“重建硬件状态”逻辑，
 * 仅恢复 I2C0/I2C1 对应端口本身，不改动已保存的 GPIO 配置。
 *
 * @param p  端口索引。
 * @return BM_OK 成功；BM_ERR_IO 仍未恢复空闲态。
 */
static int bm_vendor_i2c_recover_port(int p)
{
    i2c_dev_t           *hw;
    i2c_hal_clk_config_t clk_cfg;
    uint32_t             clk_hz;
    int                  rc;

    if (p < 0 || p >= (int)BM_VENDOR_I2C_PORT_MAX) {
        return BM_ERR_INVALID;
    }

    hw = I2C_LL_GET_HW(p);
    clk_hz = g_i2c_clk_hz[p];
    if (clk_hz == 0u) {
        clk_hz = 400000u;
    }

    /*
     * 只有物理线被拉低时才执行 9 脉冲 bus-clear。
     * SDA/SCL 均为高电平时，bus_busy 只是控制器状态残留，直接重建外设即可。
     */
    if (gpio_ll_get_level(&GPIO, (uint32_t)g_i2c_sda_gpio[p]) == 0 ||
        gpio_ll_get_level(&GPIO, (uint32_t)g_i2c_scl_gpio[p]) == 0) {
        rc = bm_vendor_i2c_bus_clear(p);
        if (rc != BM_OK) {
            return rc;
        }
    }

    BM_PERIPH_RCC_ATOMIC_BEGIN
        i2c_ll_enable_bus_clock(p, false);
        i2c_ll_enable_bus_clock(p, true);
        i2c_ll_reset_register(p);
    BM_PERIPH_RCC_ATOMIC_END

    i2c_ll_master_init(hw);
    i2c_ll_set_data_mode(hw, I2C_DATA_MODE_MSB_FIRST, I2C_DATA_MODE_MSB_FIRST);
    i2c_ll_disable_intr_mask(hw, 0xFFFFu);
    i2c_ll_clear_intr_mask(hw, 0xFFFFu);
    /* 启用 7 周期 glitch 滤波，与 port_init 一致，抑制电机噪声引起的误触发。 */
    i2c_ll_master_set_filter(hw, 7u);
    i2c_ll_master_cal_bus_clk(BM_VENDOR_I2C_APB_CLK_HZ, clk_hz, &clk_cfg);
    i2c_ll_master_set_bus_timing(hw, &clk_cfg);
    i2c_ll_txfifo_rst(hw);
    i2c_ll_rxfifo_rst(hw);

    if (i2c_ll_is_bus_busy(hw)) {
        return BM_ERR_IO;
    }
    if (gpio_ll_get_level(&GPIO, (uint32_t)g_i2c_sda_gpio[p]) == 0 ||
        gpio_ll_get_level(&GPIO, (uint32_t)g_i2c_scl_gpio[p]) == 0) {
        return BM_ERR_IO;
    }

    return BM_OK;
}

/**
 * @brief 等待 I2C 事务完成（有界忙等轮询）。
 *
 * 轮询 int_status 中的 trans_complete 或错误位（NACK / timeout / arbitration），
 * 超时后返回 BM_ERR_IO。完成后清除所有中断标志。
 * timeout / 轮询超时命中时调用 recover_port 重建控制器作为兜底。
 *
 * @param hw         I2C 硬件寄存器指针。
 * @param budget_us  最大等待时间（µs）。
 * @param port_idx   端口索引（0/1），用于 recover_port 兜底。
 * @return BM_OK 事务完成；BM_ERR_IO 超时或出错。
 */
static int bm_vendor_i2c_poll_done(i2c_dev_t *hw, uint32_t budget_us, int port_idx)
{
    uint32_t elapsed;
    uint32_t intr_st;

    for (elapsed = 0u; elapsed < budget_us; elapsed += BM_VENDOR_I2C_POLL_STEP_US) {
        /* 轮询必须读 int_raw（原始状态）：int_ena 已被 i2c_ll_disable_intr_mask 清零，
         * int_status = int_raw & int_ena 恒为 0，轮询它会永远超时（reason=4, intr=0）。 */
        intr_st = hw->int_raw.val;

        /* 检查错误位（NACK / 超时 / 仲裁丢失） */
        if ((intr_st & (uint32_t)I2C_LL_INTR_NACK) != 0u) {
            hw->int_clr.val = 0xFFFFu;
            return BM_ERR_IO;
        }
        if ((intr_st & (uint32_t)I2C_LL_INTR_TIMEOUT) != 0u) {
            hw->int_clr.val = 0xFFFFu;
            (void)bm_vendor_i2c_recover_port(port_idx);
            return BM_ERR_IO;
        }
        if ((intr_st & (uint32_t)I2C_LL_INTR_ARBITRATION) != 0u) {
            hw->int_clr.val = 0xFFFFu;
            return BM_ERR_IO;
        }

        /*
         * TRANS_COMPLETE 早于 STOP 完全释放 bus_busy 的情况在 ESP32 上可见。
         * 必须等总线真正空闲后再返回，否则下一拍会把正常的 STOP 尾段误判为卡线。
         */
        if ((intr_st & (uint32_t)I2C_LL_INTR_MST_COMPLETE) != 0u) {
            uint32_t idle_budget_us = budget_us - elapsed;
            if (bm_vendor_i2c_wait_bus_idle(hw, idle_budget_us)) {
                hw->int_clr.val = 0xFFFFu;
                return BM_OK;
            }

            hw->int_clr.val = 0xFFFFu;
            (void)bm_vendor_i2c_recover_port(port_idx);
            return BM_ERR_IO;
        }

        esp_rom_delay_us(BM_VENDOR_I2C_POLL_STEP_US);
    }

    /* 轮询超时：无任何中断标志，总线可能未翻转。 */
    hw->int_clr.val = 0xFFFFu;
    i2c_ll_txfifo_rst(hw);
    i2c_ll_rxfifo_rst(hw);
    (void)bm_vendor_i2c_recover_port(port_idx);
    return BM_ERR_IO;
}

/**
 * @brief 将 timeout_ms 转换为忙等预算（µs），并应用默认值。
 *
 * @param timeout_ms 调用方传入的超时毫秒数（0 使用默认）。
 * @return 忙等预算（µs）。
 */
static uint32_t bm_vendor_i2c_budget_us(uint32_t timeout_ms)
{
    if (timeout_ms == 0u) {
        timeout_ms = BM_VENDOR_I2C_DEFAULT_TIMEOUT_MS;
    }
    /* ms → µs，防溢出（timeout_ms 最大合理值 1000，不会溢出 uint32） */
    return timeout_ms * 1000u;
}

/**
 * @brief 事务开始前准备总线：确认空闲（必要时恢复）并复位 FIFO/中断。
 *
 * 两线均高时先等待上一笔 STOP 收尾，只有持续 busy 才恢复控制器；随后复位
 * TX/RX FIFO 并清除中断标志，使控制器处于可填充命令链的干净状态。
 *
 * @param p   端口索引。
 * @param hw  I2C 设备寄存器。
 * @return BM_OK 可开始事务；BM_ERR_IO 总线无法恢复。
 */
static int bm_vendor_i2c_prepare_bus(int p, i2c_dev_t *hw)
{
    /* 两线均高时先等待上一笔 STOP 收尾，只有持续 busy 才恢复控制器。 */
    if (i2c_ll_is_bus_busy(hw)) {
        int sda_level = gpio_ll_get_level(&GPIO, (uint32_t)g_i2c_sda_gpio[p]);
        int scl_level = gpio_ll_get_level(&GPIO, (uint32_t)g_i2c_scl_gpio[p]);
        if (!(sda_level != 0 && scl_level != 0 &&
              bm_vendor_i2c_wait_bus_idle(hw, BM_VENDOR_I2C_IDLE_WAIT_US))) {
            if (bm_vendor_i2c_recover_port(p) != BM_OK) {
                return BM_ERR_IO;
            }
        }
    }

    /* 复位 FIFO 与中断 */
    i2c_ll_txfifo_rst(hw);
    i2c_ll_rxfifo_rst(hw);
    i2c_ll_clear_intr_mask(hw, 0xFFFFu);

    return BM_OK;
}

/* ---------- 公共 API ---------- */

int bm_vendor_i2c_port_init(i2c_port_t port, gpio_num_t sda,
                            gpio_num_t scl, uint32_t clk_hz)
{
    i2c_dev_t           *hw;
    i2c_hal_clk_config_t clk_cfg;
    int                  rc;
    int                  p;

    if ((int)port < 0 || (int)port >= (int)BM_VENDOR_I2C_PORT_MAX) {
        return BM_ERR_INVALID;
    }
    if (clk_hz == 0u) {
        return BM_ERR_INVALID;
    }

    p = (int)port;

    /* 幂等：已初始化直接返回，避免重复初始化时扰动正在运行的总线。 */
    if (g_i2c_initialized[p] != 0) {
        return BM_OK;
    }

    /* 先保存端口配置，bus-clear / recover 复用同一份 GPIO 与时钟信息。 */
    g_i2c_sda_gpio[p] = sda;
    g_i2c_scl_gpio[p] = scl;
    g_i2c_clk_hz[p]   = clk_hz;

    /* 按官方 i2c_set_pin 的安全顺序，先把 SDA/SCL 置为 GPIO 开漏高电平，
     * 确保在接入 I2C matrix 之前总线处于释放态。 */
    bm_vendor_i2c_gpio_route_gpio(sda);
    bm_vendor_i2c_gpio_route_gpio(scl);
    gpio_ll_set_level(&GPIO, sda, 1u);
    gpio_ll_set_level(&GPIO, scl, 1u);
    esp_rom_delay_us(BM_VENDOR_I2C_BUS_CLEAR_SETTLE_US);

    hw = I2C_LL_GET_HW(p);

    /* 1. 使能外设总线时钟并复位（需要 __DECLARE_RCC_ATOMIC_ENV 守卫） */
    BM_PERIPH_RCC_ATOMIC_BEGIN
        i2c_ll_enable_bus_clock(p, true);
        i2c_ll_reset_register(p);
    BM_PERIPH_RCC_ATOMIC_END

    /* 2. 初始化为 master 模式（设置 ctr 寄存器：ms_mode=1, force_out=1） */
    i2c_ll_master_init(hw);

    /* 3. 禁用中断（轮询模式，不使用中断） */
    i2c_ll_disable_intr_mask(hw, 0xFFFFu);
    i2c_ll_clear_intr_mask(hw, 0xFFFFu);

    /* 4. 启用 SCL/SDA glitch 滤波器（7 个 APB 周期 ≈ 87.5 ns）。
     *    电机 PWM 通电后总线易被噪声耦合出伪 START（导致 bus_busy 残留）与误码 NACK；
     *    7 周期远小于 400 kHz 半周期(1.25 µs)，不影响真实边沿，与 esp-idf 默认一致。 */
    i2c_ll_master_set_filter(hw, 7u);

    /* 5. 计算并设置 SCL 时序（APB 周期为单位）
     *    half_cycle = APB_Hz / bus_hz / 2
     *    i2c_ll_master_cal_bus_clk 已内置公式 */
    i2c_ll_master_cal_bus_clk(BM_VENDOR_I2C_APB_CLK_HZ, clk_hz, &clk_cfg);
    i2c_ll_master_set_bus_timing(hw, &clk_cfg);

    /* 6. 复位 FIFO */
    i2c_ll_txfifo_rst(hw);
    i2c_ll_rxfifo_rst(hw);

    /* 7. 按官方顺序将 SDA / SCL 接回 I2C matrix。
     *    先完成 GPIO 开漏高电平，再挂接外设信号，可避免启用瞬间把总线拉低。 */
    bm_vendor_i2c_gpio_route_i2c(sda,
                                 g_i2c_sda_out_sig[p],
                                 g_i2c_sda_in_sig[p]);
    bm_vendor_i2c_gpio_route_i2c(scl,
                                 g_i2c_scl_out_sig[p],
                                 g_i2c_scl_in_sig[p]);

    /* 接入后若总线仍非空闲，先做 bus-clear，再重建外设并复查。 */
    if (i2c_ll_is_bus_busy(hw) ||
        gpio_ll_get_level(&GPIO, (uint32_t)sda) == 0 ||
        gpio_ll_get_level(&GPIO, (uint32_t)scl) == 0) {
        rc = bm_vendor_i2c_recover_port(p);
        if (rc != BM_OK) {
            return BM_ERR_IO;
        }
    }

    g_i2c_initialized[p] = 1;
    return BM_OK;
}

int bm_vendor_i2c_write(i2c_port_t port, uint8_t addr,
                        const uint8_t *buf, size_t len,
                        uint32_t timeout_ms)
{
    i2c_dev_t      *hw;
    i2c_ll_hw_cmd_t cmd;
    uint8_t         addr_byte;
    uint32_t        budget_us;
    int             p;

    if ((int)port < 0 || (int)port >= (int)BM_VENDOR_I2C_PORT_MAX) {
        return BM_ERR_INVALID;
    }
    if (len > 0u && buf == NULL) {
        return BM_ERR_INVALID;
    }
    /* write_len + 1(addr) 不得超过 FIFO 深度 */
    if (len + 1u > (size_t)SOC_I2C_FIFO_LEN) {
        return BM_ERR_INVALID;
    }

    p         = (int)port;
    hw        = I2C_LL_GET_HW(p);
    budget_us = bm_vendor_i2c_budget_us(timeout_ms);

    /* 事务前准备总线：确认空闲（必要时恢复）并复位 FIFO/中断。 */
    if (bm_vendor_i2c_prepare_bus(p, hw) != BM_OK) {
        return BM_ERR_IO;
    }

    /* 填充 TX FIFO：地址字节（addr<<1 | 0 = WRITE）+ 数据 */
    addr_byte = (uint8_t)((addr << 1u) | 0u);
    i2c_ll_write_txfifo(hw, &addr_byte, 1u);
    if (len > 0u) {
        i2c_ll_write_txfifo(hw, buf, (uint8_t)len);
    }

    /* 设置命令寄存器链 */
    /* cmd[0]: START */
    memset(&cmd, 0, sizeof(cmd));
    cmd.op_code = I2C_LL_CMD_RESTART;
    i2c_ll_master_write_cmd_reg(hw, cmd, 0);

    /* cmd[1]: WRITE(addr+W + data)，启用 ACK 检测 */
    memset(&cmd, 0, sizeof(cmd));
    cmd.op_code  = I2C_LL_CMD_WRITE;
    cmd.byte_num = (uint8_t)(len + 1u);
    cmd.ack_en   = 1u;
    cmd.ack_exp  = 0u;  /* 期望从设备回 ACK（0） */
    i2c_ll_master_write_cmd_reg(hw, cmd, 1);

    /* cmd[2]: STOP */
    memset(&cmd, 0, sizeof(cmd));
    cmd.op_code = I2C_LL_CMD_STOP;
    i2c_ll_master_write_cmd_reg(hw, cmd, 2);

    /* cmd[3]: END（终止命令链） */
    memset(&cmd, 0, sizeof(cmd));
    cmd.op_code = I2C_LL_CMD_END;
    i2c_ll_master_write_cmd_reg(hw, cmd, 3);

    /* 启动事务 */
    i2c_ll_master_trans_start(hw);

    return bm_vendor_i2c_poll_done(hw, budget_us, p);
}

int bm_vendor_i2c_write_read(i2c_port_t port, uint8_t addr,
                             const uint8_t *write_buf, size_t write_len,
                             uint8_t *read_buf, size_t read_len,
                             uint32_t timeout_ms)
{
    i2c_dev_t      *hw;
    i2c_ll_hw_cmd_t cmd;
    uint8_t         addr_byte;
    uint32_t        budget_us;
    int             rc;
    int             cmd_idx;
    int             p;

    if ((int)port < 0 || (int)port >= (int)BM_VENDOR_I2C_PORT_MAX) {
        return BM_ERR_INVALID;
    }
    if ((write_len > 0u && write_buf == NULL) ||
        (read_len  > 0u && read_buf  == NULL)) {
        return BM_ERR_INVALID;
    }
    /* 写阶段 FIFO：addr(W) + write_buf */
    if (write_len + 1u > (size_t)SOC_I2C_FIFO_LEN) {
        return BM_ERR_INVALID;
    }
    /* 读阶段 FIFO：addr(R) + read_buf，addr 字节在写侧 FIFO */
    if (read_len > (size_t)SOC_I2C_FIFO_LEN) {
        return BM_ERR_INVALID;
    }

    p         = (int)port;
    hw        = I2C_LL_GET_HW(p);
    budget_us = bm_vendor_i2c_budget_us(timeout_ms);

    /* 事务前准备总线：确认空闲（必要时恢复）并复位 FIFO/中断。 */
    if (bm_vendor_i2c_prepare_bus(p, hw) != BM_OK) {
        return BM_ERR_IO;
    }

    /* 填充写阶段 TX FIFO */
    addr_byte = (uint8_t)((addr << 1u) | 0u);  /* WRITE 地址 */
    i2c_ll_write_txfifo(hw, &addr_byte, 1u);
    if (write_len > 0u) {
        i2c_ll_write_txfifo(hw, write_buf, (uint8_t)write_len);
    }

    /* 读阶段地址也放入 TX FIFO */
    addr_byte = (uint8_t)((addr << 1u) | 1u);  /* READ 地址 */
    i2c_ll_write_txfifo(hw, &addr_byte, 1u);

    cmd_idx = 0;

    /* cmd[0]: START */
    memset(&cmd, 0, sizeof(cmd));
    cmd.op_code = I2C_LL_CMD_RESTART;
    i2c_ll_master_write_cmd_reg(hw, cmd, cmd_idx++);

    /* cmd[1]: WRITE(addr+W + write_buf) */
    memset(&cmd, 0, sizeof(cmd));
    cmd.op_code  = I2C_LL_CMD_WRITE;
    cmd.byte_num = (uint8_t)(write_len + 1u);
    cmd.ack_en   = 1u;
    cmd.ack_exp  = 0u;
    i2c_ll_master_write_cmd_reg(hw, cmd, cmd_idx++);

    /* cmd[2]: RESTART（repeated start） */
    memset(&cmd, 0, sizeof(cmd));
    cmd.op_code = I2C_LL_CMD_RESTART;
    i2c_ll_master_write_cmd_reg(hw, cmd, cmd_idx++);

    /* cmd[3]: WRITE(addr+R，1 字节） */
    memset(&cmd, 0, sizeof(cmd));
    cmd.op_code  = I2C_LL_CMD_WRITE;
    cmd.byte_num = 1u;
    cmd.ack_en   = 1u;
    cmd.ack_exp  = 0u;
    i2c_ll_master_write_cmd_reg(hw, cmd, cmd_idx++);

    if (read_len > 1u) {
        /* cmd[4]: READ(N-1 字节，发 ACK） */
        memset(&cmd, 0, sizeof(cmd));
        cmd.op_code  = I2C_LL_CMD_READ;
        cmd.byte_num = (uint8_t)(read_len - 1u);
        cmd.ack_val  = 0u;  /* 发 ACK */
        i2c_ll_master_write_cmd_reg(hw, cmd, cmd_idx++);
    }

    /* cmd[last-2]: READ(1 字节，发 NACK，最后一字节） */
    memset(&cmd, 0, sizeof(cmd));
    cmd.op_code  = I2C_LL_CMD_READ;
    cmd.byte_num = 1u;
    cmd.ack_val  = 1u;  /* 发 NACK */
    i2c_ll_master_write_cmd_reg(hw, cmd, cmd_idx++);

    /* cmd[last-1]: STOP */
    memset(&cmd, 0, sizeof(cmd));
    cmd.op_code = I2C_LL_CMD_STOP;
    i2c_ll_master_write_cmd_reg(hw, cmd, cmd_idx++);

    /* cmd[last]: END */
    memset(&cmd, 0, sizeof(cmd));
    cmd.op_code = I2C_LL_CMD_END;
    i2c_ll_master_write_cmd_reg(hw, cmd, cmd_idx++);

    /* 启动事务 */
    i2c_ll_master_trans_start(hw);

    rc = bm_vendor_i2c_poll_done(hw, budget_us, p);
    if (rc != BM_OK) {
        return rc;
    }

    /* 从 RX FIFO 读取数据 */
    i2c_ll_read_rxfifo(hw, read_buf, (uint8_t)read_len);

    return BM_OK;
}
