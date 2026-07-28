/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_instances_stm32g4.h
 * @brief STM32G474xB 板级实例绑定头（默认 NUCLEO-G474RE 参考板）
 *
 * 本头集中保存全部实例特定默认值（外设实例 / GPIO / 通道 / AF），
 * vendor .c 文件一律经这些宏取硬件绑定，不写死任何 GPIO 或通道号。
 * 应用工程可在包含本头前（或经编译期 -D）覆盖任意宏以适配自有板卡。
 *
 * 不直接依赖 CMSIS 头：寄存器指针型默认值（TIM6 等）在 .c 内结合
 * stm32g4xx.h 使用，本头只放可独立包含的宏常量。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（STM32G474xB 移植）
 * 2026-07-27       1.1            zeh            修正 BM_STM32G4_ADC_JEXTSEL 默认值 2→8（TIM1_TRGO2
 *                                                正确编码，LL 常量佐证）；新增 PWM/ENC GPIO AF 宏
 * 2026-07-27       1.2            zeh            接口批 1：新增 SPI1/USART2 设备实例宏
 *
 */
#ifndef BM_HAL_INSTANCES_STM32G4_H
#define BM_HAL_INSTANCES_STM32G4_H

/* ---------- 系统 tick（默认 TIM6 基本定时器，APB1 定时器时钟 170MHz） ---------- */

/**
 * @brief tick 定时器选择：默认 TIM6。
 *
 * 若板卡 TIM6 已被 DAC 等占用，定义 BM_STM32G4_TICK_USE_TIM7 切到 TIM7
 * （寄存器布局与 TIM6 一致）。ISR 入口与 RCC 使能位由 singleton 按此宏切换。
 */
/* #define BM_STM32G4_TICK_USE_TIM7 */

/** @brief tick ISR 的 NVIC 优先级（数值越小优先级越高，默认 2）。 */
#ifndef BM_STM32G4_TICK_IRQ_PRIORITY
#define BM_STM32G4_TICK_IRQ_PRIORITY  2u
#endif

/* ---------- Console UART（默认 LPUART1，NUCLEO-G474RE ST-LINK VCP：PA2/PA3） ---------- */

/** @brief console 串口波特率。 */
#ifndef BM_STM32G4_UART_BAUD
#define BM_STM32G4_UART_BAUD  115200u
#endif
/** @brief console TX 引脚：PA2（LPUART1_TX，AF12）。 */
#ifndef BM_STM32G4_UART_TX_PIN
#define BM_STM32G4_UART_TX_PIN  2u
#endif
/** @brief console RX 引脚：PA3（LPUART1_RX，AF12）。 */
#ifndef BM_STM32G4_UART_RX_PIN
#define BM_STM32G4_UART_RX_PIN  3u
#endif
/** @brief console TX/RX 复用功能号（LPUART1 @ PA2/PA3 = AF12）。 */
#ifndef BM_STM32G4_UART_GPIO_AF
#define BM_STM32G4_UART_GPIO_AF  12u
#endif
/** @brief UART RX 中断 NVIC 优先级。 */
#ifndef BM_STM32G4_UART_IRQ_PRIORITY
#define BM_STM32G4_UART_IRQ_PRIORITY  3u
#endif

/* ---------- 三相 PWM（默认 TIM1 三相互补，NUCLEO-G474RE：PA8/PA9/PA10 + PB13/PB14/PB15） ---------- */

/** @brief PWM 载波频率（Hz），中心对齐。 */
#ifndef BM_STM32G4_PWM_FREQ_HZ
#define BM_STM32G4_PWM_FREQ_HZ  20000u
#endif
/** @brief PWM 占空比满量程（set_duty 入参量程 0..MAX，内部按比例映射到 ARR）。 */
#ifndef BM_STM32G4_PWM_DUTY_MAX
#define BM_STM32G4_PWM_DUTY_MAX  1000u
#endif
/** @brief 互补输出死区时间（ns），写入 TIM1 BDTR.DTG。 */
#ifndef BM_STM32G4_PWM_DEADTIME_NS
#define BM_STM32G4_PWM_DEADTIME_NS  100u
#endif
/** @brief PWM 高边 GPIO 引脚号（A/B/C 相：PA8/PA9/PA10，AF6）。 */
#ifndef BM_STM32G4_PWM_GPIO_AF
#define BM_STM32G4_PWM_GPIO_AF  6u
#endif
#ifndef BM_STM32G4_PWM_UH_PIN
#define BM_STM32G4_PWM_UH_PIN  8u
#endif
#ifndef BM_STM32G4_PWM_VH_PIN
#define BM_STM32G4_PWM_VH_PIN  9u
#endif
#ifndef BM_STM32G4_PWM_WH_PIN
#define BM_STM32G4_PWM_WH_PIN  10u
#endif
/** @brief PWM 低边 GPIO 引脚号（A/B/C 相：PB13/PB14/PB15，AF6）。 */
#ifndef BM_STM32G4_PWM_UL_PIN
#define BM_STM32G4_PWM_UL_PIN  13u
#endif
#ifndef BM_STM32G4_PWM_VL_PIN
#define BM_STM32G4_PWM_VL_PIN  14u
#endif
#ifndef BM_STM32G4_PWM_WL_PIN
#define BM_STM32G4_PWM_WL_PIN  15u
#endif
/** @brief TIM1 update 中断（电流环回调）NVIC 优先级。 */
#ifndef BM_STM32G4_PWM_IRQ_PRIORITY
#define BM_STM32G4_PWM_IRQ_PRIORITY  1u
#endif

/* ---------- 相电流 ADC（默认 ADC1 injected 双 rank，TIM1 TRGO2 触发） ---------- */

/** @brief ADC 注入序列 rank 数（ia/ib 两路，第三相由基尔霍夫定律重构）。 */
#ifndef BM_STM32G4_ADC_RANK_COUNT
#define BM_STM32G4_ADC_RANK_COUNT  2u
#endif
/** @brief ia 采样通道（默认 ADC1_IN1 @ PA0 模拟输入）。 */
#ifndef BM_STM32G4_ADC_CH_IA
#define BM_STM32G4_ADC_CH_IA  1u
#endif
/** @brief ib 采样通道（默认 ADC1_IN2 @ PA1 模拟输入）。 */
#ifndef BM_STM32G4_ADC_CH_IB
#define BM_STM32G4_ADC_CH_IB  2u
#endif
/** @brief ia GPIO 引脚号（GPIOA）。 */
#ifndef BM_STM32G4_ADC_IA_PIN
#define BM_STM32G4_ADC_IA_PIN  0u
#endif
/** @brief ib GPIO 引脚号（GPIOA）。 */
#ifndef BM_STM32G4_ADC_IB_PIN
#define BM_STM32G4_ADC_IB_PIN  1u
#endif
/**
 * @brief ADC 注入外部触发源（ADC_JSQR.JEXTSEL 5bit 编码）。
 *
 * 默认 8 = TIM1_TRGO2（RM0440 ADC1/2 注入触发源表；与 LL 常量
 * LL_ADC_INJ_TRIG_EXT_TIM1_TRGO2 = ADC_JSQR_JEXTSEL_3 编码一致）。
 * 与 PWM 侧 TRGO2 联动——TIM1 update 事件经 TRGO2 触发注入采样，
 * 中心对齐模式下 update 位于计数谷底=低边采样窗口。
 * 改触发源时须与 PWM 侧 TRGO2 配置一并核对 RM0440 编码表（实机验收项）。
 */
#ifndef BM_STM32G4_ADC_JEXTSEL
#define BM_STM32G4_ADC_JEXTSEL  8u
#endif
/** @brief ADC 注入通道采样时间（ADC_SMPRx.SMPx 编码，默认 12.5 周期=010b）。 */
#ifndef BM_STM32G4_ADC_SMP
#define BM_STM32G4_ADC_SMP  2u
#endif
/** @brief ADC JEOS 中断 NVIC 优先级（与 PWM update 同级，避免嵌套打断 FPU 现场）。 */
#ifndef BM_STM32G4_ADC_IRQ_PRIORITY
#define BM_STM32G4_ADC_IRQ_PRIORITY  1u
#endif

/* ---------- 编码器（默认 TIM3 正交编码器模式，PA6/PA7 AF2） ---------- */

/** @brief 编码器线数（CPR），TIM3 计数一圈 = 4×CPR。 */
#ifndef BM_STM32G4_ENC_CPR
#define BM_STM32G4_ENC_CPR  4096u
#endif
/** @brief 编码器 A/B 相 GPIO 引脚号（GPIOA：PA6/PA7，AF2）。 */
#ifndef BM_STM32G4_ENC_GPIO_AF
#define BM_STM32G4_ENC_GPIO_AF  2u
#endif
#ifndef BM_STM32G4_ENC_A_PIN
#define BM_STM32G4_ENC_A_PIN  6u
#endif
#ifndef BM_STM32G4_ENC_B_PIN
#define BM_STM32G4_ENC_B_PIN  7u
#endif

/* ---------- 过流比较器（默认 COMP1 输出内部直连 TIM1_BKIN） ---------- */

/**
 * @brief COMP1 同相输入选择（CSR.INPSEL）：0=PA1，1=PB1。
 * 默认 0（PA1，与 ib 采样共用引脚拓扑，板级按实际布线覆盖）。
 */
#ifndef BM_STM32G4_COMP_INPSEL
#define BM_STM32G4_COMP_INPSEL  0u
#endif
/**
 * @brief COMP1 反相输入选择（CSR.INMSEL，4bit 编码）。
 * 默认 1 = 1/2 VREFINT；DAC 门限等其它选项按 RM0440 COMP1 INMSEL 表覆盖。
 */
#ifndef BM_STM32G4_COMP_INMSEL
#define BM_STM32G4_COMP_INMSEL  1u
#endif
/** @brief COMP1 迟滞（CSR.HYST 编码，0=无迟滞；默认 2 档抗噪）。 */
#ifndef BM_STM32G4_COMP_HYST
#define BM_STM32G4_COMP_HYST  2u
#endif
/** @brief COMP1 输出极性（CSR.POLARITY：0=不反相，触发 TIM1 break 高有效）。 */
#ifndef BM_STM32G4_COMP_POLARITY
#define BM_STM32G4_COMP_POLARITY  0u
#endif
/** @brief COMP1 blanking 源（CSR.BLANKING 编码，默认 0=不 blanking）。 */
#ifndef BM_STM32G4_COMP_BLANKING
#define BM_STM32G4_COMP_BLANKING  0u
#endif

/* ---------- 看门狗（IWDG，独立 LSI ~32kHz） ---------- */

/** @brief IWDG 时钟（LSI 典型值 32kHz，实机按实测 LSI 频偏校准）。 */
#ifndef BM_STM32G4_LSI_HZ
#define BM_STM32G4_LSI_HZ  32000u
#endif

/* ---------- SPI1（编码器等，SCK/MISO/MOSI + 软件 CS） ---------- */

/** @brief SPI1 时钟（Hz），vendor 按 PCLK2 就近取分频档。 */
#ifndef BM_STM32G4_SPI1_CLOCK_HZ
#define BM_STM32G4_SPI1_CLOCK_HZ  1000000u
#endif
/** @brief SPI1 模式（BM_SPI_MODE_0..3；AS5047P 为模式 1）。 */
#ifndef BM_STM32G4_SPI1_MODE
#define BM_STM32G4_SPI1_MODE  1u
#endif
/** @brief SPI1 GPIO AF 编码（PA5/PA6/PA7 = AF5）。 */
#ifndef BM_STM32G4_SPI1_GPIO_AF
#define BM_STM32G4_SPI1_GPIO_AF  5u
#endif
/** @brief SPI1 SCK/MISO/MOSI 引脚号（GPIOA，默认 PA5/PA6/PA7）。 */
#ifndef BM_STM32G4_SPI1_SCK_PIN
#define BM_STM32G4_SPI1_SCK_PIN   5u
#endif
#ifndef BM_STM32G4_SPI1_MISO_PIN
#define BM_STM32G4_SPI1_MISO_PIN  6u
#endif
#ifndef BM_STM32G4_SPI1_MOSI_PIN
#define BM_STM32G4_SPI1_MOSI_PIN  7u
#endif
/** @brief SPI1 软件 CS 引脚号（GPIOA，默认 PA4；CS 由 GPIO 设备操作）。 */
#ifndef BM_STM32G4_SPI1_CS_PIN
#define BM_STM32G4_SPI1_CS_PIN  4u
#endif

/* ---------- USART3 设备实例（RS485 等，支持 IDLE + DMA TX/RX） ---------- */

/** @brief USART3 波特率。 */
#ifndef BM_STM32G4_USART3_BAUD
#define BM_STM32G4_USART3_BAUD  115200u
#endif
/** @brief USART3 GPIO AF 编码（默认 PB10/PB11 = AF7）。 */
#ifndef BM_STM32G4_USART3_GPIO_AF
#define BM_STM32G4_USART3_GPIO_AF  7u
#endif
/** @brief USART3 TX 端口（0=A,1=B,2=C,3=D,4=E,5=F,6=G；默认 GPIOB）。 */
#ifndef BM_STM32G4_USART3_TX_PORT
#define BM_STM32G4_USART3_TX_PORT  1u
#endif
/** @brief USART3 TX 引脚号（默认 PB10）。 */
#ifndef BM_STM32G4_USART3_TX_PIN
#define BM_STM32G4_USART3_TX_PIN  10u
#endif
/** @brief USART3 RX 端口（0=A,1=B,2=C,3=D,4=E,5=F,6=G；默认 GPIOB）。 */
#ifndef BM_STM32G4_USART3_RX_PORT
#define BM_STM32G4_USART3_RX_PORT  1u
#endif
/** @brief USART3 RX 引脚号（默认 PB11）。 */
#ifndef BM_STM32G4_USART3_RX_PIN
#define BM_STM32G4_USART3_RX_PIN  11u
#endif
/** @brief USART3 TX DMA 控制器（1=DMA1,2=DMA2；默认 DMA1）。 */
#ifndef BM_STM32G4_USART3_TX_DMA_CTRL
#define BM_STM32G4_USART3_TX_DMA_CTRL  1u
#endif
/** @brief USART3 TX DMA 通道号（1-based，默认 DMA1_CH4）。 */
#ifndef BM_STM32G4_USART3_TX_DMA_CH
#define BM_STM32G4_USART3_TX_DMA_CH  4u
#endif
/** @brief USART3 TX DMAMUX 请求号（RM0440：USART3_TX=27）。 */
#ifndef BM_STM32G4_USART3_TX_DMA_REQ
#define BM_STM32G4_USART3_TX_DMA_REQ  27u
#endif
/** @brief USART3 RX DMA 控制器（1=DMA1,2=DMA2；默认 DMA1）。 */
#ifndef BM_STM32G4_USART3_RX_DMA_CTRL
#define BM_STM32G4_USART3_RX_DMA_CTRL  1u
#endif
/** @brief USART3 RX DMA 通道号（1-based，默认 DMA1_CH5）。 */
#ifndef BM_STM32G4_USART3_RX_DMA_CH
#define BM_STM32G4_USART3_RX_DMA_CH  5u
#endif
/** @brief USART3 RX DMAMUX 请求号（RM0440：USART3_RX=28）。 */
#ifndef BM_STM32G4_USART3_RX_DMA_REQ
#define BM_STM32G4_USART3_RX_DMA_REQ  28u
#endif
/** @brief USART3 全局中断 NVIC 优先级。 */
#ifndef BM_STM32G4_USART3_IRQ_PRIORITY
#define BM_STM32G4_USART3_IRQ_PRIORITY  3u
#endif
/** @brief USART3 TX DMA 完成中断 NVIC 优先级。 */
#ifndef BM_STM32G4_USART3_TX_DMA_IRQ_PRIORITY
#define BM_STM32G4_USART3_TX_DMA_IRQ_PRIORITY  3u
#endif
/** @brief USART3 RX DMA 完成中断 NVIC 优先级。 */
#ifndef BM_STM32G4_USART3_RX_DMA_IRQ_PRIORITY
#define BM_STM32G4_USART3_RX_DMA_IRQ_PRIORITY  3u
#endif

/* ---------- USART2 设备实例（TMC2209 等，支持单线半双工） ---------- */

/** @brief USART2 波特率（TMC2209 常用 115200）。 */
#ifndef BM_STM32G4_USART2_BAUD
#define BM_STM32G4_USART2_BAUD  115200u
#endif
/** @brief USART2 单线半双工使能（非零：HDSEL，仅 TX 脚，TMC 单线拓扑）。 */
#ifndef BM_STM32G4_USART2_SINGLE_WIRE
#define BM_STM32G4_USART2_SINGLE_WIRE  1u
#endif
/** @brief USART2 GPIO AF 编码（PA9/PA10 = AF7）。 */
#ifndef BM_STM32G4_USART2_GPIO_AF
#define BM_STM32G4_USART2_GPIO_AF  7u
#endif
/** @brief USART2 TX/RX 引脚号（GPIOA，默认 PA9/PA10；与 PWM 高边默认脚
 * 冲突，同用 PWM 与 USART2 的板级须覆盖其一）。 */
#ifndef BM_STM32G4_USART2_TX_PIN
#define BM_STM32G4_USART2_TX_PIN  9u
#endif
#ifndef BM_STM32G4_USART2_RX_PIN
#define BM_STM32G4_USART2_RX_PIN  10u
#endif
/** @brief USART2 RX 中断 NVIC 优先级。 */
#ifndef BM_STM32G4_USART2_IRQ_PRIORITY
#define BM_STM32G4_USART2_IRQ_PRIORITY  3u
#endif

/* ---------- DMA 通道/请求（SPI1 异步 DMA、USART2 RX DMA；G4 DMAMUX 请求号） ---------- */

/** @brief SPI1 DMA RX 通道号（1-based，默认 DMA1_CH1；改通道须同步 vendor 内 ISR 映射注释）。 */
#ifndef BM_STM32G4_SPI1_DMA_RX_CH
#define BM_STM32G4_SPI1_DMA_RX_CH  1u
#endif
/** @brief SPI1 DMA TX 通道号（1-based，默认 DMA1_CH2）。 */
#ifndef BM_STM32G4_SPI1_DMA_TX_CH
#define BM_STM32G4_SPI1_DMA_TX_CH  2u
#endif
/** @brief SPI1 DMAMUX 请求号（RM0440 DMAMUX 表：SPI1_RX=10，SPI1_TX=11）。 */
#ifndef BM_STM32G4_SPI1_DMA_RX_REQ
#define BM_STM32G4_SPI1_DMA_RX_REQ  10u
#endif
#ifndef BM_STM32G4_SPI1_DMA_TX_REQ
#define BM_STM32G4_SPI1_DMA_TX_REQ  11u
#endif
/** @brief USART2 RX DMA 通道号（1-based，默认 DMA1_CH3）。 */
#ifndef BM_STM32G4_USART2_RX_DMA_CH
#define BM_STM32G4_USART2_RX_DMA_CH  3u
#endif
/** @brief USART2 RX DMAMUX 请求号（RM0440：USART2_RX=26）。 */
#ifndef BM_STM32G4_USART2_RX_DMA_REQ
#define BM_STM32G4_USART2_RX_DMA_REQ  26u
#endif
/** @brief SPI1 RX DMA 完成中断 NVIC 优先级。 */
#ifndef BM_STM32G4_SPI1_DMA_IRQ_PRIORITY
#define BM_STM32G4_SPI1_DMA_IRQ_PRIORITY  2u
#endif
/** @brief USART2 RX DMA 半满/全满中断 NVIC 优先级。 */
#ifndef BM_STM32G4_USART2_RX_DMA_IRQ_PRIORITY
#define BM_STM32G4_USART2_RX_DMA_IRQ_PRIORITY  3u
#endif

/* ---------- 时间基（DWT CYCCNT） ---------- */

/** @brief CPU 标称主频（Hz），uptime 纳秒换算与 cpu_freq points 单点共用。 */
#ifndef BM_STM32G4_CPU_FREQ_HZ
#define BM_STM32G4_CPU_FREQ_HZ  170000000u
#endif

/* ---------- FDCAN（默认 FDCAN1 PB8/PB9，FDCAN2 PB12/PB13，500k Classic CAN） ---------- */

/** @brief FDCAN1 TX 引脚号（GPIOB，默认 PB8）。 */
#ifndef BM_STM32G4_CAN1_TX_PIN
#define BM_STM32G4_CAN1_TX_PIN  8u
#endif
/** @brief FDCAN1 RX 引脚号（GPIOB，默认 PB9）。 */
#ifndef BM_STM32G4_CAN1_RX_PIN
#define BM_STM32G4_CAN1_RX_PIN  9u
#endif
/** @brief FDCAN2 TX 引脚号（GPIOB，默认 PB12）。 */
#ifndef BM_STM32G4_CAN2_TX_PIN
#define BM_STM32G4_CAN2_TX_PIN  12u
#endif
/** @brief FDCAN2 RX 引脚号（GPIOB，默认 PB13）。 */
#ifndef BM_STM32G4_CAN2_RX_PIN
#define BM_STM32G4_CAN2_RX_PIN  13u
#endif
/** @brief FDCAN GPIO 复用功能号（PB8/PB9/PB12/PB13 均为 AF9）。 */
#ifndef BM_STM32G4_CAN_GPIO_AF
#define BM_STM32G4_CAN_GPIO_AF  9u
#endif
/** @brief FDCAN 中断 NVIC 优先级。 */
#ifndef BM_STM32G4_CAN_IRQ_PRIORITY
#define BM_STM32G4_CAN_IRQ_PRIORITY  2u
#endif
/** @brief FDCAN1 在全局 Message RAM 中的 32-bit word 偏移。 */
#ifndef BM_STM32G4_CAN1_MSG_RAM_OFFSET
#define BM_STM32G4_CAN1_MSG_RAM_OFFSET  0u
#endif
/** @brief FDCAN2 在全局 Message RAM 中的 32-bit word 偏移。
 *  FDCAN1 区域预留 256 words。 */
#ifndef BM_STM32G4_CAN2_MSG_RAM_OFFSET
#define BM_STM32G4_CAN2_MSG_RAM_OFFSET  256u
#endif
/** @brief FDCAN 仲裁段位时序：500k @ 170MHz APB1（采样点 80%）。 */
#ifndef BM_STM32G4_CAN_NBTR
#define BM_STM32G4_CAN_NBTR  { .prescaler = 17u, .tseg1 = 15u, .tseg2 = 3u, .sjw = 1u }
#endif

#endif /* BM_HAL_INSTANCES_STM32G4_H */
