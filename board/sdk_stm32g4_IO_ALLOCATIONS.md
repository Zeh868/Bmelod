# sdk_stm32g4 产品 I/O 与资源门禁

日期：2026-07-27
状态：**Task 0 有条件通过；产品硬件输入未闭合，Task 2+ 锁止**

## 1. 适用范围与结论

仓库当前没有可识别的 `board/<product>/` 产品目录，也没有产品原理图、物料
型号或已批准的产品板配置。现有资料仅把 `sdk_stm32g4` 默认后端描述为
STM32G474xB / NUCLEO-G474RE 参考绑定：

- `portable/vendor/stm32g4/bm_hal_instances_stm32g4.h`
- `portable/vendor/stm32g4/README.md`
- `board/bm_board_envelope_stm32g4.h`
- `board/sdk_stm32g4_CHECKLIST.md`

因此本文只冻结**仓库可证的参考基线、冲突和缺失输入**，不把参考绑定冒充
产品分配，也不虚构产品目录或硬件参数。

结论：

- Task 0 的扫描与门禁记录已形成，可作为后续补齐产品输入的唯一受跟踪清单。
- 产品输入未达到可实施状态；所有硬件相关 Task 2+ 均不得开始。
- Task 1 是独立的纯仓库 NVS capability 修复，不受本门禁阻塞。
- 产品输入补齐后，须由主控重新审查本文的资源矩阵、静态 SRAM 预算和角色
  命名，审查通过才可解锁 Task 2；本文当前不批准任何实现改动。

## 2. 已有参考资源矩阵

下表来自当前实例头和 vendor 实现，只表示默认参考绑定。`—` 表示当前实现
未占用；“待产品确认”不是可用资源。

| 角色 | 外设/通道 | Pin / AF | DMA / 请求 | IRQ / 优先级 | 结论 |
|---|---|---|---|---|---|
| framework tick | TIM6 update；可切 TIM7 | — | — | `TIM6_DAC_IRQn` 或 `TIM7_IRQn` / 2 | TIM6 向量与 DAC 共用；若产品占用 DAC，须决定 TIM6/TIM7 |
| console | LPUART1 | PA2 TX、PA3 RX / AF12 | — | `LPUART1_IRQn` / 3 | TX 轮询；RX ISR 逐字节回调 |
| 三相 PWM 高边 | TIM1 CH1/2/3 | PA8/PA9/PA10 / AF6 | — | `TIM1_UP_TIM16_IRQn` / 1 | PA9/PA10 与 USART2 冲突 |
| 三相 PWM 低边 | TIM1 CH1N/2N/3N | PB13/PB14/PB15 / AF6 | — | 同上 | 与高边共同占用 TIM1 |
| 相电流 ia/ib | ADC1 injected IN1/IN2 | PA0/PA1 / analog | — | `ADC1_2_IRQn` / 1 | TIM1 TRGO2 触发，rank 数为 2 |
| 过流保护 | COMP1 → TIM1_BKIN | PA1 同相输入；反相默认内部 1/2 VREFINT | — | 硬件 break，无独立软件 IRQ | PA1 与 ADC1_IN2 是刻意共享的模拟拓扑，产品原理图必须确认 |
| 增量编码器 | TIM3 CH1/CH2 | PA6/PA7 / AF2 | — | — | 轮询读计数；PA6/PA7 与 SPI1 冲突 |
| SPI 设备 | SPI1 + 软件 CS | PA5 SCK、PA6 MISO、PA7 MOSI / AF5；PA4 CS | **配置宏**：DMA1 CH1 RX / 请求 10；CH2 TX / 请求 11 | RX NVIC 由 `DMA1_Channel1_IRQn + (RX_CH - 1)` 计算；默认解析为 `DMA1_Channel1_IRQn` / 2，handler 支持 RX CH1/CH2 | 与 TIM3 编码器冲突；当前只有单控制器全局异步状态 |
| TMC/UART 设备 | USART2，默认单线半双工 | PA9 TX、PA10 RX / AF7 | **配置宏**：DMA1 CH3 RX / 请求 26 | USART2 / 3；DMA NVIC 由 `DMA1_Channel1_IRQn + (RX_CH - 1)` 计算，默认在 G474 CMSIS 中解析为 `DMA1_Channel3_IRQn` / 3；实际只编译 `DMA1_Channel3_IRQHandler` | 与 TIM1 PWM 高边冲突；DMA 缓冲块由调用者提供 |
| 看门狗 | IWDG / LSI | — | — | — | LSI 仅有 32 kHz 参考值，产品需实测频偏 |
| 单调时间 | DWT CYCCNT | — | — | — | 依赖产品实际 CPU 时钟；当前默认 170 MHz |
| 通用 GPIO 设备 | GPIOA–GPIOG | 未分配 | — | 不支持 GPIO IRQ 绑定 | 不能据此认定任意 pin 可供新角色使用 |

### 2.1 已确认的冲突

1. **PA6/PA7**：TIM3 编码器 AF2 与 SPI1 MISO/MOSI AF5 互斥。产品若同时需要
   两个角色，必须在原理图和目标封装可用 AF 的基础上迁移其中一组。
2. **PA9/PA10**：TIM1 PWM CH2/CH3 AF6 与 USART2 TX/RX AF7 互斥。产品若同时
   需要三相 PWM 与 USART2，必须迁移 UART 或 PWM 角色。
3. **配置层的 DMA1 CH1/CH2/CH3** 已分别分配给 SPI1 RX/TX、USART2 RX。
   这是 descriptor 宏的默认资源占用，不等于三个通道都有同名 handler：
   SPI 仅由 RX 通道 IRQ 收尾，USART2 RX 只提供 CH3 handler。新增 FDCAN、
   USART3、STEP 或 Flash 路径不得复用这些配置通道，除非产品矩阵明确取消
   原角色或给出可验证的仲裁设计。
4. `TIM1_UP_TIM16_IRQn`、`TIM6_DAC_IRQn`、`ADC1_2_IRQn` 是共享向量；产品
   若启用向量中的另一外设，必须在资源表中显式列出并验证仲裁。

### 2.2 DMA 配置与 NVIC 映射风险

审查 `bm_hal_instances_stm32g4.h`、USART2 RX DMA 与 SPI DMA 实现后，结论是：

- USART2 RX 默认宏为 DMA1 CH3。驱动启用/禁用/设优先级使用
  `DMA1_Channel1_IRQn + (BM_STM32G4_USART2_RX_DMA_CH - 1)`；当前
  STM32G474 CMSIS 设备头中 CH1/CH2/CH3 IRQn 分别为 11/12/13，因此默认
  表达式确实解析到 `DMA1_Channel3_IRQn`，与
  `DMA1_Channel3_IRQHandler` 一致。
- SPI1 RX 默认宏为 CH1，TX 为 CH2；同一 IRQn 算法默认解析到 CH1。
  SPI 只使能 RX TC IRQ，并为 RX CH1/CH2 提供条件编译 handler。
- 所以当前默认 G474 配置**未证实存在 CH1 与 CH3 的实际错绑**。但是两处
  实现都依赖 CMSIS IRQn 连续编号，且 handler 覆盖范围小于配置宏可表达的
  通道范围；USART2 RX 非 CH3 会直接触发 `#error`。仓库不内置或锁定
  CMSIS 设备头版本，实际构建仍须用应用提供的 Cube 包交叉编译确认。

该耦合列为既有高风险：进入 Task 2 的 board schema 前必须单独裁决，是把
通道限制写入 descriptor 校验，还是改为显式 IRQn/handler 映射。裁决前不得
把 DMA 通道宏描述为可任意覆盖，也不得仅凭配置宏宣称 NVIC 行为已验证。

## 3. 产品输入缺失清单

以下任一项缺失都不能靠默认值或猜测补齐：

| 分支 | 必需输入 | 当前状态 |
|---|---|---|
| 产品身份 | 产品/板卡名称、原理图版本、BOM 版本、目标 MCU 完整料号与封装、可用 SRAM 分区和链接脚本 | **缺失**；仅有通用 `STM32G474xx` 宏与 NUCLEO 参考链接脚本路径 |
| CAN/FDCAN transport | FDCAN 实例、TX/RX pin/AF、收发器和使能/静默脚、DMA/IRQ、经典/FD 模式、仲裁/数据 bit rate 与采样点、过滤器和 RX/TX 深度 | **缺失** |
| CAN 编码器协议 | DBC 或可审查帧表、CAN ID/帧格式、字节序、缩放/偏置、方向/回绕、序号与错误位、刷新周期和 timeout、故障升级语义 | **缺失** |
| USART3 RS485 | USART3 pin/AF、收发器型号、DE/RE 接法及有效电平、终端/偏置、IRQ/DMA、波特率/帧格式、TC 后撤 DE 时序 | **缺失** |
| RS485 网络 | 35 节点地址表、帧边界/CRC、主从/广播应答规则、轮询周期、timeout/retry、最坏帧长和满载带宽预算 | **缺失** |
| STEP/DIR | STEP/DIR/EN pin、电平语义、驱动器型号、最小高/低脉宽、DIR setup/hold、最大 step rate、专用 TIM/通道/IRQ | **缺失** |
| 共享 SPI | 产品实际 SPI 控制器、各从设备 mode/最大时钟/CS pin 与电平、DMA/IRQ、总线所有权和 timeout | **缺失**；参考 SPI1 与编码器冲突 |
| SPI Flash | Flash 完整型号/容量、JEDEC ID、page/sector/block 大小、擦写时序与寿命、供电掉电约束、分区地址/长度/对齐、A/B 槽尺寸 | **缺失** |
| DMA/NVIC 映射策略 | descriptor 是否限制固定通道，或提供显式 IRQn/handler 映射；不得依赖未校验的 IRQn 算术 | **未裁决**；见 2.2 节 |
| 验证资源 | 实机、ST-Link/调试器、CAN 对端、RS485 对端/逻辑分析仪、示波器、断电注入装置 | **未登记** |

## 4. API、返回值与调用方基线

扫描命令：

```powershell
rg -n "bm_hal_(encoder_read|uart_|spi_|nvs_|timer_)" include Source tests
```

截至 2026-07-27，该命令得到 214 个匹配行（含声明、实现、注释、native
测试辅助 API）。生产调用关系如下：

| API 族 | 生产调用方与当前行为 |
|---|---|
| `bm_hal_encoder_read` | `motor_foc_sensored`；实例/后端缺失为 `BM_ERR_NOT_INIT`，输出指针 NULL 为 `BM_ERR_INVALID`，否则透传平台错误码 |
| `bm_hal_uart_*` | `tmc2209` 使用 `send/recv`；QEMU smoke 使用默认 console；hard-RT profile 下阻塞发送返回 `BM_ERR_NOT_SUPPORTED`，接收为 0，RX 回调注册 fail-closed |
| `bm_hal_spi_*` | `abs_encoder` 使用同步 transfer；未绑定为 `BM_ERR_NOT_INIT`，无异步能力为 `BM_ERR_NOT_SUPPORTED`，非法缓冲/长度为 `BM_ERR_INVALID` |
| `bm_hal_nvs_*` | 仅 `bm_persist` 调用；load/save 是同步接口。`sdk_stm32g4` 已提供双槽 Flash 后端（`BM_DRV_HAS_NVS_BACKEND`）；Board 须先 `bm_nvs_stm32g4_set_layout`，且槽基地址/大小须按当前页大小对齐（双 Bank 2KB、单 Bank 4KB）、互不重叠、位于有效 Flash 范围内。App 链接脚本必须显式保留这两段 Flash 空间。无 NVS 后端构建下 `bm_persist` 保持 RAM KV，`commit` 返回 `BM_ERR_NOT_SUPPORTED`（非静默成功） |
| `bm_hal_timer_*` | `bm_hrt`、`bm_exec`、`bm_mp*`、watchdog/时间辅助和测试使用；现有周期 timer 不等价于 Task 6 的一次性 deadline timer |

裸负值断言扫描发现：

- `test_algo_filter.c` 的 resampler 0/-1 业务返回；
- `test_transport_qos_token.c` 的四处 enqueue 接受/丢弃 0/-1 业务语义；
- `test_stepper_pulse.c` 的方向状态 `-1`；
- `test_tinyml_adapter.c` 的两个 int8 载荷 `-1`。

没有裸 `-1` 断言指向上述 encoder/UART/SPI/NVS/timer HAL。后续若改变
resampler 或 transport QoS 语义，必须把这些断言纳入风险清单；状态型 HAL
仍须使用 `BM_OK/BM_ERR_*`。

## 5. ISR 上下文基线

| ISR | ISR 内当前动作 | 约束 |
|---|---|---|
| TIM6/TIM7 update | tick 计数递增并调用已注册 timer callback | callback 在 ISR；不得阻塞 |
| LPUART1 / USART2 | 读 RX 字节并逐字节调用 RX callback | callback 在 ISR；当前 TMC 消费逻辑应留在周期上下文 |
| DMA1 CH1/CH2（SPI RX 映射） | 收尾 DMA/CS/状态并调用异步完成 callback | callback 在 ISR；不得重入占用控制器 |
| DMA1 CH3（USART2 RX） | 半满/全满时调用 stream binding | binding 在 ISR；block 由调用者静态提供 |
| TIM1 update | 调用 PWM update binding | HRT ISR，只允许有界工作 |
| ADC1_2 JEOS | 缓存 injected rank 后调用 complete binding | HRT ISR，只缓存/派发，不做协议业务 |
| TIM3 encoder read | 无 ISR，调用方同步读取计数器 | CAN 编码器不得假设沿用该上下文 |
| NVS/persist | 同步接口，文档明确不允许 ISR 调用 | Flash 擦写必须位于非 RT、非 ISR 路径 |

## 6. 静态 SRAM 门禁

当前仓库不能给出 CAN/UART/SPI/Flash **产品上限**，原因是目标完整料号/封装、
链接脚本、buffer/ring 深度及后续 descriptor/runtime 布局都未冻结。现有代码
只能提供以下可证基线：

| 域 | 当前可证静态占用 | 产品上限结论 |
|---|---|---|
| CAN | 无 CAN HAL/DRV/vendor 对象 | RX/TX ring 深度、帧宽、统计和 FDCAN message RAM 均未知，**不可计算** |
| UART | console 与 USART2 各有 callback 指针和 ready 标志；USART2 DMA 仅持有 binding、设备/active block 指针和 1 字节 FPU 守卫，RX block 由调用者提供 | USART3 RX/TX ring、35 节点表、最大帧和重试槽未知，**不可计算** |
| SPI | 当前 SPI1 只持有初始化/异步 owner、callback/user、busy、dummy 字节和 1 字节 FPU 守卫；不静态复制用户 TX/RX buffer | 多从设备 descriptor 数、Flash staging/verify buffer 和排队策略未知，**不可计算** |
| Flash/NVS 上游 | 默认 persist 配置为 16 条、key 15、value 64；序列化 blob 为 `6 + 16 × (1 + 16 + 2 + 64) + 4 = 1338` 字节。按 STM32 32 位 ABI，RAM KV 表为 `16 × 84 = 1344` 字节，加初始化标志后，上游已知静态量为 **2683 字节** | 该数不含 Flash backend、A/B metadata/staging、链接器对齐和应用其它 RAM；Flash 产品上限仍**不可计算** |

在产品给出完整输入后，预算必须同时列出：

```text
CAN = RX_DEPTH * sizeof(frame) + TX_DEPTH * sizeof(frame) + runtime/stats
UART = RX_RING + TX_RING + 35 * sizeof(node_state) + parser/retry buffers
SPI = controller runtime + device descriptors + 最大在途/校验缓冲
Flash = persist 2683 B + A/B backend runtime + staging/verify buffers
```

四域之和须与**实际链接 map 的 `.data + .bss`** 和目标 SRAM 分区比较；任一
上限超过批准预算或无法证明有界，继续锁止对应硬件任务，不得换用堆规避。

## 7. API alignment 基线

执行：

```powershell
python tools/check_api_alignment.py
```

结果：退出码 `1`，`Missing APIs (29)`。29 项与仓库 `AGENTS.md` 登记的存量
基线一致，包括 atomic 3 项、event 9 项、critical 2 项、mempool 2 项、
module 4 项、shell 6 项、watchdog 3 项。本 Task 没有把该存量问题报告为全绿，
也没有修改脚本或 API。

## 8. 解锁检查项

补齐第 3 节输入后，主控必须逐项确认：

- [ ] 产品名、原理图/BOM 版本、完整 MCU 料号与封装已冻结；
- [ ] 所有现有与新增角色均有 pin/AF/DMA/IRQ/TIM 分配；
- [ ] PA6/PA7、PA9/PA10 冲突已有经原理图验证的迁移方案；
- [ ] CAN、RS485、STEP、SPI Flash 的协议和电气/时序参数完整；
- [ ] DMA/NVIC 映射策略已裁决，配置宏允许范围与实际 handler 覆盖一致；
- [ ] 四域静态 SRAM 上限已按最终结构计算，并由链接 map 验证不超预算；
- [ ] Task 2 的 board descriptor 文件名和角色实例名由产品名派生并获批准；
- [ ] 主控审查结论为“通过”后，才解锁 Task 2；后续各分支仍按各自依赖门禁。
