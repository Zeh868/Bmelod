# vendor/stm32g4 — STM32G474xB（NUCLEO-G474RE 参考板）裸机外设后端

本目录是 STM32G474xB 的裸机外设驱动层，外设驱动基于 **STM32 LL
（Low-Layer）库** 实现（刻意不定义 `USE_HAL_DRIVER`，LL 独立可用；
LL 头文件与所用 `.c` 由 `cmake/bm_sdk_stm32g4.cmake` 的
`bm_sdk_stm32g4_apply()` 从 Cube 包注入并编入目标）。
经 `BM_BACKEND=sdk_stm32g4` 由 `portable/packs/sdk_stm32g4` 组合
`arch/armv7em` + 本目录一起引入。CMSIS/LL 由应用工程经
`-DBM_STM32_CUBE_PATH=<STM32CubeG4 根目录>` 提供——注意
`Drivers/STM32G4xx_HAL_Driver` 是 STM32CubeG4 的 submodule，
稀疏检出后须 `git submodule update --init Drivers/STM32G4xx_HAL_Driver`。

## 实现路线（LL 库）

用到的 LL 模块（`stm32g4xx_ll_*.h`，均头文件内联）：

| LL 模块 | 用途 |
|---|---|
| `ll_bus` | AHB/APB 外设时钟使能 |
| `ll_rcc` | LSI 使能/就绪、APB 分频查询、`LL_RCC_GetSystemClocksFreq()`（唯一非内联调用，`stm32g4xx_ll_rcc.c` 经 `BM_STM32G4_LL_SOURCES` 编入，`USE_FULL_LL_DRIVER` 门控） |
| `ll_gpio` | 引脚模式/AF/上下拉；EXTI 经 SYSCFG 映射 + EXTI RTSR/FTSR/IMR |
| `ll_tim` | TIM6 tick、TIM1 三相互补 PWM（中心对齐/OC/死区/TRGO2/BKIN）、TIM3 编码器 |
| `ll_lpuart` | LPUART1 波特率/收发/RXNE 中断 |
| `ll_usart` | USART2 设备实例（HDSEL 单线半双工） |
| `ll_dma` / `ll_dmamux` | SPI1 异步 DMA、USART2 RX DMA 通道/请求配置 |
| `ll_spi` | SPI1 主机全双工（软件 NSS，同步 + DMA 异步） |
| `ll_iwdg` | IWDG 预分频/重装载/启动/喂狗 |
| `ll_adc` | ADC1 注入序列/触发源/校准/JEOS 中断 |
| `ll_comp` | COMP1 输入选择/迟滞/极性/blanking |

保留 CMSIS 写法的位置（LL 无对应 API）：NVIC 优先级/使能（CMSIS core
`NVIC_*` 函数）与 DWT CYCCNT 时间基（CoreDebug/DWT 属 CMSIS Core 外设）。
不定义 `USE_FULL_ASSERT`：LL `.c` 中 `assert_param` 为空操作，无需提供
`assert_failed`。

## 当前驱动能力

| 驱动文件 | 说明 |
|---|---|
| `bm_vendor_singleton_stm32g4.c` | timer（TIM6/TIM7 周期 update ISR tick）/ UART（LPUART1 轮询收发 + RX 中断回调）/ WDG（IWDG @LSI）/ `bm_hal_uptime_ns_raw()`（DWT CYCCNT @170MHz，64 位扩展） |
| `bm_vendor_pwm_stm32g4.c` | 三相互补 PWM（TIM1 中心对齐 + 死区 + update ISR 电流环回调；COMP1→TIM1_BKIN 硬件过流 break） |
| `bm_vendor_adc_stm32g4.c` | 相电流 ADC（ADC1 注入组 ia/ib 双 rank，TIM1 TRGO2 硬件触发，JEOS ISR 缓存+回调） |
| `bm_vendor_encoder_stm32g4.c` | 增量编码器（TIM3 正交编码器模式 3，4×CPR 计数） |
| `bm_vendor_comp_stm32g4.c` | 过流比较器（COMP1，`clear_latch` 清 TIM1 break 锁存） |
| `bm_vendor_gpio_stm32g4.c` | GPIO 设备（bm_drv_gpio 契约，全 GPIOA-G 口，pin 编码 (port<<4)\|num）；**EXTI 已实现**（SYSCFG 端口映射、边沿/IMR、NVIC EXTI0..4/9_5/15_10，同 line 端口冲突返回 `BM_ERR_BUSY`） |
| `bm_vendor_spi_stm32g4.c` | SPI1 阻塞全双工（软件 CS 经 GPIO 设备，时钟/模式可配） |
| `bm_vendor_uart_dev_stm32g4.c` | USART2 设备实例（统一 bm_hal_uart 实例契约，HDSEL 单线半双工，TMC2209 用） |
| `bm_vendor_dma_usart2_rx_stm32g4.c` | USART2 RX DMA 块流设备（bm_drv_dma_stream 契约，DMA1 循环模式 + half/full 回调） |
| `bm_hal_cpu_freq_stm32g4.c` | CPU 主频三接口（真机规则：`freq_hz()` 读 `SystemCoreClock`，单点 170MHz，`set` 恒 `BM_ERR_NOT_SUPPORTED`） |

导出实例（对齐 esp32 vendor 命名）：`bm_hal_pwm_m0` / `bm_hal_adc_m0` /
`bm_hal_encoder_m0` / `bm_hal_comp_m0`；接口批 1 设备：`bm_stm32g4_gpio`
（全芯片 GPIO）、`bm_stm32g4_spi1`、`bm_stm32g4_uart_dev_usart2`。

## 实例绑定

全部板级默认值（外设实例 / GPIO / 通道 / AF / 频率 / 门限编码）集中在
`bm_hal_instances_stm32g4.h` 的可覆盖宏，默认对齐 **NUCLEO-G474RE**：

| 信号 | 默认绑定 |
|---|---|
| tick | TIM6 update 中断（可 `BM_STM32G4_TICK_USE_TIM7` 切 TIM7） |
| console | LPUART1 @ PA2/PA3（ST-LINK VCP，AF12，115200 8N1），导出 `bm_uart_default` |
| PWM 三相 | TIM1 CH1/2/3 + CH1N/2N/3N @ PA8/PA9/PA10 + PB13/PB14/PB15（AF6），20kHz 中心对齐 |
| 相电流 | ADC1 注入 rank0=IN1(PA0) / rank1=IN2(PA1)，TIM1 TRGO2 触发 |
| 编码器 | TIM3 CH1/CH2 @ PA6/PA7（AF2），CPR 4096 |
| 过流 | COMP1（INP=PA1，INM=1/2 VREFINT）→ TIM1_BKIN（AF1.BKCMP1E） |

覆盖方式：应用工程在包含实例头前 `#define` 同名宏，或编译期 `-D`。
注意默认定时器复用拓扑固定为高边 GPIOA / 低边 GPIOB / 编码器 GPIOA，
跨端口换脚属板级改动（须同步核对 AF 编码，实机验收项）。

## 采样/保护链路（RM0440 语义）

```text
TIM1 update（中心对齐谷底=低边采样窗口）→ TRGO2 → ADC1 注入序列 → JEOS ISR
                                                      ↓
                                          read_injected 读缓存（电流环）
COMP1（过流门限）→ TIM1_BKIN（AF1.BKCMP1E）→ 硬件 break 立即关 MOE（无软件延迟）
```

契约铁律（与框架 `01-HAL契约与移植要点.md` §混合域外设要点一致）：
`bind_update`/`bind_complete` 只开关中断源，**不**隐式使能 PWM 输出、
**不**启动 ADC 序列；`binding==NULL` 先关中断源再清回调。

## 上层集成

```cmake
set(BM_BACKEND "sdk_stm32g4" CACHE STRING "")
add_subdirectory(Bmelod)
target_link_libraries(my_app PRIVATE bm_framework bm_hal_stm32g4)
```

启动文件/链接脚本经 `BM_STM32G4_STARTUP` / `BM_STM32G4_LD` 从 Cube 包引用
（见 `cmake/bm_sdk_stm32g4.cmake` 头注释）；`system_stm32g4xx.c` 由应用工程
从 Cube 引入（vendor 不接管时钟树）。最小 smoke 工程（compile+link 验证）
见仓库 `build_tests_stm32g4/smoke/`（gitignore 覆盖的本地验证稿）。

## 成熟度与已知缺口（E1）

当前 **E1**：外设语义按 RM0440 与 LL 库实现，交叉编译/链接已验证，
**未做实机时序/PIL/WCET 验收**（按 `board/sdk_stm32g4_CHECKLIST.md` 移交）。

已知缺口：

- NVS/flash 持久化无后端；pack 不声明 `BM_DRV_HAS_NVS_BACKEND`，
  `bm_persist` 保持 RAM KV，commit 为成功 no-op；
- DMA stream、I2C/SPI 传感器挂接未实现（实际需要再补；批 2 I2C/DAC/CAN
  为后续独立方案）；
- GPIO AF 配置不在 bm_drv_gpio 契约内（AF 属各外设 vendor 内部）；
  **GPIO EXTI 已实现**（`bm_vendor_gpio_stm32g4.c`，可选
  `bm_gpio_stm32g4_config_t.irq_priority`）；定时器设备实例契约未建（stepper_pulse
  经 resources 回调规避，实机由业务/vendor 绑一路 TIM）；
- UART TX DMA 未实现（console 打印量小，登记缺口；RX DMA 已有
  bm_stm32g4_usart2_rx_dma）；SPI DMA 仅 SPI1（transfer_async），
  多设备 DMA 调度未做；
- SPI1（PA5/6/7）与编码器 TIM3（PA6/7）、USART2（PA9/10）与 PWM 高边
  （PA8/9/10）默认引脚冲突，同用须覆盖其一（instances 宏）；
- COMP 门限/blanking 编码（`BM_STM32G4_COMP_*`）按 RM0440 表给出默认值，
  **实机须核对**；门限若改用 DAC 通道（INMSEL 编码 4/5），DAC 本体配置
  由板级自备；
- ADC 读值为 12bit raw，板级零偏中心化/标定未做（对齐 esp32 vendor 的
  中心化机制待实机标定后补）；
- PWM 死区仅支持 DTG 第一编码段（≤127 tDTS，约 ≤747ns @170MHz）；
- LPUART/USART2 时钟源按默认 PCLK1 推算，改过 `CCIPR.LPUART1SEL` 的板级需实机核对。
