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
 * @author Kimi Code CLI
 * @version 2.0
 * @date 2026-06-21
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-21       1.0            Kimi           vendor 层共享硬件 I2C（IDF legacy 实现）
 * 2026-06-21       2.0            Sonnet         改为 LL 寄存器级裸机实现，零 RTOS 依赖
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

/* ---------- 静态辅助函数 ---------- */

/**
 * @brief 配置单个 GPIO 为 I2C 开漏输出，并通过 GPIO matrix 路由到 I2C 外设。
 *
 * @param gpio_num  GPIO 编号。
 * @param out_sig   GPIO matrix 输出信号编号（I2C 外设→GPIO）。
 * @param in_sig    GPIO matrix 输入信号编号（GPIO→I2C 外设）。
 */
static void bm_vendor_i2c_gpio_init(gpio_num_t gpio_num, int out_sig, int in_sig)
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

    /* GPIO matrix 输出路由：I2C 信号 → GPIO
     * oen_sel 必须为 1：sda_force_out/scl_force_out=1 时外设只输出数据信号，
     * 输出使能需由 GPIO_ENABLE_REG（gpio_ll_output_enable）提供。
     * oen_sel=0 会导致输出使能信号缺失，I2C 无法拉低总线。 */
    GPIO.func_out_sel_cfg[gpio_num].func_sel    = (uint32_t)out_sig;
    GPIO.func_out_sel_cfg[gpio_num].inv_sel     = 0u;
    GPIO.func_out_sel_cfg[gpio_num].oen_sel     = 1u;
    GPIO.func_out_sel_cfg[gpio_num].oen_inv_sel = 0u;

    /* GPIO matrix 输入路由：GPIO → I2C 信号 */
    GPIO.func_in_sel_cfg[in_sig].sig_in_sel = 1u;
    GPIO.func_in_sel_cfg[in_sig].func_sel   = (uint32_t)gpio_num;
    GPIO.func_in_sel_cfg[in_sig].sig_in_inv = 0u;
}

/**
 * @brief 等待 I2C 事务完成（有界忙等轮询）。
 *
 * 轮询 int_status 中的 trans_complete 或错误位（NACK / timeout / arbitration），
 * 超时后返回 BM_ERR_IO。完成后清除所有中断标志。
 *
 * @param hw         I2C 硬件寄存器指针。
 * @param budget_us  最大等待时间（µs）。
 * @return BM_OK 事务完成；BM_ERR_IO 超时或出错。
 */
static int bm_vendor_i2c_poll_done(i2c_dev_t *hw, uint32_t budget_us)
{
    uint32_t elapsed;
    uint32_t intr_st;

    for (elapsed = 0u; elapsed < budget_us; elapsed += BM_VENDOR_I2C_POLL_STEP_US) {
        intr_st = hw->int_status.val;

        /* 检查错误位（NACK / 超时 / 仲裁丢失） */
        if ((intr_st & (uint32_t)I2C_LL_INTR_NACK) != 0u) {
            hw->int_clr.val = 0xFFFFu;
            return BM_ERR_IO;
        }
        if ((intr_st & (uint32_t)I2C_LL_INTR_TIMEOUT) != 0u) {
            hw->int_clr.val = 0xFFFFu;
            return BM_ERR_IO;
        }
        if ((intr_st & (uint32_t)I2C_LL_INTR_ARBITRATION) != 0u) {
            hw->int_clr.val = 0xFFFFu;
            return BM_ERR_IO;
        }

        /* 事务完成 */
        if ((intr_st & (uint32_t)I2C_LL_INTR_MST_COMPLETE) != 0u) {
            hw->int_clr.val = 0xFFFFu;
            return BM_OK;
        }

        esp_rom_delay_us(BM_VENDOR_I2C_POLL_STEP_US);
    }

    /* 超时：复位 FIFO，清中断 */
    hw->int_clr.val = 0xFFFFu;
    i2c_ll_txfifo_rst(hw);
    i2c_ll_rxfifo_rst(hw);
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

/* ---------- 公共 API ---------- */

int bm_vendor_i2c_port_init(i2c_port_t port, gpio_num_t sda,
                            gpio_num_t scl, uint32_t clk_hz)
{
    i2c_dev_t           *hw;
    i2c_hal_clk_config_t clk_cfg;
    int                  p;

    if ((int)port < 0 || (int)port >= (int)BM_VENDOR_I2C_PORT_MAX) {
        return BM_ERR_INVALID;
    }
    if (clk_hz == 0u) {
        return BM_ERR_INVALID;
    }

    p = (int)port;

    /* 幂等：已初始化直接返回 */
    if (g_i2c_initialized[p] != 0) {
        return BM_OK;
    }

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

    /* 4. 关闭 glitch filter（400 kHz fast-mode 下可关）*/
    i2c_ll_master_set_filter(hw, 0u);

    /* 5. 计算并设置 SCL 时序（APB 周期为单位）
     *    half_cycle = APB_Hz / bus_hz / 2
     *    i2c_ll_master_cal_bus_clk 已内置公式 */
    i2c_ll_master_cal_bus_clk(BM_VENDOR_I2C_APB_CLK_HZ, clk_hz, &clk_cfg);
    i2c_ll_master_set_bus_timing(hw, &clk_cfg);

    /* 6. 复位 FIFO */
    i2c_ll_txfifo_rst(hw);
    i2c_ll_rxfifo_rst(hw);

    /* 7. 配置 SDA / SCL GPIO（开漏 + 上拉 + GPIO matrix 路由）
     *    信号编号来自 gpio_sig_map.h 宏常量，零运行时查表、零额外链接依赖。 */
    bm_vendor_i2c_gpio_init(sda,
                            g_i2c_sda_out_sig[p],
                            g_i2c_sda_in_sig[p]);
    bm_vendor_i2c_gpio_init(scl,
                            g_i2c_scl_out_sig[p],
                            g_i2c_scl_in_sig[p]);

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

    /* 复位 FIFO 与中断 */
    i2c_ll_txfifo_rst(hw);
    i2c_ll_rxfifo_rst(hw);
    i2c_ll_clear_intr_mask(hw, 0xFFFFu);

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

    return bm_vendor_i2c_poll_done(hw, budget_us);
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

    /* 复位 FIFO 与中断 */
    i2c_ll_txfifo_rst(hw);
    i2c_ll_rxfifo_rst(hw);
    i2c_ll_clear_intr_mask(hw, 0xFFFFu);

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

    rc = bm_vendor_i2c_poll_done(hw, budget_us);
    if (rc != BM_OK) {
        return rc;
    }

    /* 从 RX FIFO 读取数据 */
    i2c_ll_read_rxfifo(hw, read_buf, (uint8_t)read_len);

    return BM_OK;
}
