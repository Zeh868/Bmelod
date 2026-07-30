# 08 HAL契约与移植要点

> **本文职责**：HAL 契约、driver API 分层、参考平台与混合域外设移植要点。  
> **不负责**：Keil/IAR 加源文件 → [06-Keil](06-Keil-MDK集成.md)、[07-IAR](07-IAR-EWARM集成.md)；挂库总览 → [02-挂库](02-挂库到现有工程.md)。

应用与 `Source/` 只依赖 `include/bm_hal_*.h`；厂商 Port 在 `portable/`（量产用 `template/bm_port.c` 接 SDK）。混合域 **bind 如何接到向量 ISR** 见 [03 §3.1](../01-应用开发/05-混合域接线.md#31-直接-hal-绑定不用-bm_exec)。

## 三层结构（driver API + 架构 Port）

```text
include/hal/bm_hal_*.h      应用契约（稳定 API）
include/drv/bm_drv_*.h      驱动 API 表（Zephyr 同构 vtable）
include/port/               架构层契约（bm_arch_ops.h、bm_port_arch.h）
Source/hal/                 分发层（契约 → driver API）
portable/arch/              ISA 层（临界区 / 屏障，类比 FreeRTOS port.c）
portable/vendor/            芯片 SDK 外设层
portable/sim/               仿真后端（qemu_cm0、qemu_riscv64）
portable/packs/             BM_BACKEND 兼容包（组合 arch + vendor + sim）
portable/template/          量产 Port 模板
```

| 层级 | 职责 |
|------|------|
| 契约 `bm_hal_*` | 应用可见 API，不暴露 SDK 类型 |
| 驱动 API `bm_drv_*` | 子系统函数表 + 设备 `{api, config}` |
| 架构 `portable/arch/<id>/` | `bm_drv_critical_api`、`bm_drv_memory_api`（ISA 绑定） |
| vendor | Timer/UART/PWM 等对接 Cube / IDF |
| 分发层 | 未链接后端时返回 `BM_ERR_NOT_INIT` |
| `packs/` | 将 arch + vendor 链为 `bm_hal_<platform>`，保持 `BM_BACKEND` 不变 |

## 参考平台

| 后端目录 | 定位 |
|----------|------|
| `portable/sim/native` | PC 测试与示例（`packs/native_sim` = arch/host + sim/native） |
| `portable/vendor/esp32_idf` | 灯哥平衡车主控板 ESP32-WROOM-32E（`packs/sdk_esp32_idf` = arch/xtensa + vendor）；**集成步骤** → [08-ESP-IDF与灯哥平衡车集成](08-ESP-IDF与灯哥平衡车集成.md) |
| `portable/vendor/stm32g4` | STM32G474xB（NUCLEO-G474RE 参考板）STM32 LL 库外设（`packs/sdk_stm32g4` = arch/armv7em + vendor/stm32g4）；成熟度 E1，细节 → [vendor README](../../portable/vendor/stm32g4/README.md) |
| `portable/vendor/ch32v003` | **暂缓** — CH32V003 外设（后续恢复 `packs/register_ch32v003`） |
| `portable/vendor/nordic_nrf52` | **暂缓** — nRF52 外设桩（链 `arch/armv7em`，无 pack） |
| `portable/vendor/nxp_kinetis` | **暂缓** — Kinetis 外设桩（链 `arch/armv7em`，无 pack） |
| `portable/packs/qemu_cortex_m0` | QEMU M0（arch/armv6m + sim/qemu_cm0） |
| `portable/packs/qemu_riscv64` | QEMU RV64 virt（arch/riscv64 + sim/qemu_riscv64） |
| `portable/boot/qemu_cortex_m0/` | QEMU M0 启动、`crt0`、链接脚本 |
| `portable/boot/riscv64/` | QEMU RV64 virt 启动、`crt0`、链接脚本 |
| `portable/template/bm_port.c` | 量产 Port 模板 |

## CMake 链接

```cmake
# 推荐：显式 arch + vendor（新）
set(BM_PORT_ARCH "xtensa" CACHE STRING "")
set(BM_PORT_VENDOR "esp32_idf" CACHE STRING "")

# 兼容：BM_BACKEND 解析为 packs/ 下的组合包
set(BM_BACKEND "native_sim" CACHE STRING "")
add_subdirectory(bmelod-baremetal)
target_link_libraries(my_app PRIVATE bm_framework bm_hal_native)         # PC
target_link_libraries(my_app PRIVATE bm_framework bm_hal_esp32wroom32e)  # 灯哥 ESP32-WROOM-32E
```

| `BM_BACKEND` | 解析 |
|--------------|------|
| `native_sim` | `packs/native_sim` |
| `sdk_esp32_idf` | `arch/xtensa` + `vendor/esp32_idf` |
| `arch_stub` | 仅 `arch/stub`，HAL 烟雾 |
| `sdk_stm32g4` | `arch/armv7em` + `vendor/stm32g4`（CMSIS 经 `BM_STM32_CUBE_PATH` 注入） |
| `register_ch32v003` | 暂缓，当前不随 `portable/vendor` 提供 |

详见 [03-Port移植层bm_port](03-Port移植层bm_port.md)。

**务必链接后端库**（`bm_hal_<platform>` 为 `bm_backend_*` 别名）。仅链 `bm_hal` 分发层时：

- `bm_hal_adc_bind_complete` 等返回 `BM_ERR_NOT_INIT`
- 无向量 ISR，绑定了也不会进电流环

见 [03-运行时约束与排障](../04-测试与排障/03-运行时约束与排障.md) fail-stop 说明。

## 混合域外设要点

| API | 行为 |
|-----|------|
| `bind_complete` / `bind_update` | 保存 `bm_hal_hrt_binding_t` 到 HAL 静态变量；ISR 调 `callback(context)` |
| `binding == NULL` | 先关中断源，再清 callback |
| 绑定 | **不得**隐式使能 PWM 输出或启动 ADC 序列 |

PWM/ADC/COMP/Encoder 契约头文件：`bm_hal_pwm.h`、`bm_hal_adc.h` 等。
接口批 1 新增：`bm_hal_gpio.h`（整芯片单设备，pin 编码 `(port<<4)|num`，
无中断/AF 配置）、`bm_hal_spi.h`（阻塞全双工 transfer + 可选
`transfer_async`，config 含时钟/模式/CS GPIO）。

**UART 统一实例模型**：`bm_hal_uart_init(const bm_hal_uart_t *uart, …)`
四函数全部实例化，无单例全局符号；每个后端导出一个默认控制台设备
`bm_uart_default`（native_sim/qemu 各 sim/esp32/stm32g4 各一，声明在
`bm_hal_uart.h`、仅 `BM_DRV_HAS_BACKEND` 时可见），console 与既有调用点
统一迁移到 `&bm_uart_default`；其余 UART（TMC2209/RS485）由 vendor 以
同型设备导出（如 stm32g4 的 `bm_stm32g4_uart_dev_usart2`）。

**DMA 语义**：SPI `transfer_async` 为可选成员（NULL = 分发层返回
`BM_ERR_NOT_SUPPORTED`），done_cb 于 ISR 上下文触发（FPU 守卫包裹），
回调时 tx/rx 缓冲区所有权归还调用方；UART RX DMA 复用既有
`bm_hal_dma_stream` 契约（bind_rx/submit_rx/detach_rx + half/full 回调），
stm32g4 导出 `bm_stm32g4_usart2_rx_dma`；UART TX DMA 暂无实现（登记缺口）。

移植检查：实现本应用用到的 API → `native_sim` 单测 → 真机时序。

## CPU 主频接口（bm_hal_cpu_freq_*）

`include/hal/bm_hal_cpu.h` 在既有 CPU 抽象（ID/Bootstrap/从核启动/临界区让步）
之外，另有 3 个主频接口——**每个 port 移植时必须实现**（无框架默认，仅由桩
`Source/hal/bm_hal_cpu_stub.c` 兜底 native/qemu 一类没有真实时钟硬件的后端）：

```c
uint32_t bm_hal_cpu_freq_hz(void);
int bm_hal_cpu_freq_points(const uint32_t **points, uint32_t *count);
int bm_hal_cpu_freq_set(uint32_t hz);
```

- `bm_hal_cpu_freq_hz()`：查询当前主频（Hz），运行期真值。
- `bm_hal_cpu_freq_points()`：声明本芯片支持的频率点集合（DVFS 候选）；
  `points` 指向 port 内部静态点表，`count>=1`；参数为 `NULL` 返回
  `BM_ERR_INVALID`。不支持 DVFS 的 port 返回单点（当前频率）。
- `bm_hal_cpu_freq_set(hz)`：将主频切到某个点（本版是移植层占位机制，供
  未来 PM 子系统调用）。`hz` 须为 `freq_points` 之一；返回 `BM_OK`（易实现
  的 port 记录并让 `freq_hz` 反映之）、`BM_ERR_NOT_SUPPORTED`（真机/SDK 拥
  有时钟的 port 暂未接 PM）或 `BM_ERR_INVALID`（`hz` 不在支持集内）。

### 各类 port 的实现规则

**sim/qemu 仿真类 port**（`portable/sim/native`、`native_mp` 及五个
`portable/sim/qemu_*_smp` 后端）统一规则——因为这类 port 仿真的是"被
config 配置的目标"，让开机对账在 host 上也能一致通过：

- `freq_hz()` 返回内部记录值，初值取 `BM_CONFIG_CPU_FREQ_HZ`；
- `freq_points()` **镜像 config 点集**：应用定义了
  `BM_CONFIG_CPU_DVFS_POINTS_HZ` 就原样返回它，否则退化为单点
  `{ BM_CONFIG_CPU_FREQ_HZ }`；
- `freq_set(hz)` 校验 `hz` 落在点集内后记录、更新 `freq_hz` 返回值，返回
  `BM_OK`（真做，只是没有真实时钟硬件可切）。

**真机 port**（如 `portable/vendor/esp32_idf/bm_hal_cpu_freq_esp32.c`）走
相反规则——主频由 SDK 时钟树拥有，本版不接管：

- `freq_hz()` 直接查 SDK 运行期真值（ESP32 是 `esp_clk_cpu_freq()`）；
- `freq_points()` 声明芯片真实支持的档位（ESP32 是 `{80000000u,
  160000000u, 240000000u}`，与 IDF sdkconfig 三档一致），**不**镜像
  config；
- `freq_set(hz)` 恒返回 `BM_ERR_NOT_SUPPORTED`——真实切频待后续 PM 子系统
  接入 `esp_pm`/`rtc_clk` 等 SDK API 再实现，本版只占位。

**桩与真机 port 的去重约定 `BM_HAL_CPU_HAS_PORT_FREQ`**：`bm_hal_cpu_stub.c`
默认提供这 3 个函数（服务 native/qemu 等未单独实现的后端），但真机 port
若自带专属实现文件（如 esp32 的 `bm_hal_cpu_freq_esp32.c`），桩文件据
`#ifndef BM_HAL_CPU_HAS_PORT_FREQ` 让出这 3 个符号，避免与 port 专属实现
重复定义、链接失败。**关键点：该宏必须到达桩文件 `bm_hal_cpu_stub.c` 所在
编译单元才有效**——桩属 `bm_hal` 目标，`bm_hal` 只链 `bm_config`、并不
依赖真机 port 所在的 vendor 目标（依赖方向是 vendor→bm_config），所以在
vendor 目标上加 `target_compile_definitions(... PUBLIC
BM_HAL_CPU_HAS_PORT_FREQ)`（如 `portable/vendor/esp32_idf/CMakeLists.txt`
里那条）**不足以**传到桩：PUBLIC 编译定义只会传给链接了该 vendor 目标的
消费方，而 `bm_hal` 不是它的消费方。真正可靠的途径是让消费方 IDF 工程
**全局**定义这个宏——idf.py 组件级 `-D BM_HAL_CPU_HAS_PORT_FREQ`，或经
`bm_config.h` 的 include 链（如在其 `bm_config_app.h` 里 `#define`）——
使其在编译 `bm_hal_cpu_stub.c` 时可见。**移植新真机 port 且自带 freq
实现时务必按此方式全局声明这个宏**；未生效会在链接期报桩与 port 专属实现
重复定义。

### 开机对账 `bm_hal_cpu_freq_check_config()`

`Source/hal/bm_hal_cpu_freq.c` 提供一个共享（非 port）的开机自检门面：

```c
int bm_hal_cpu_freq_check_config(void);
```

它把 config 层的静态声明（`BM_CONFIG_CPU_FREQ_HZ` /
`BM_CONFIG_CPU_DVFS_POINTS_HZ`）与 port 层的运行期真值（`bm_hal_cpu_freq_hz()`
/ `bm_hal_cpu_freq_points()`）喂给纯逻辑校验函数 `bm_hal_cpu_freq_check()`，
做三条一致性检查，任一不满足返回 `BM_ERR_INVALID`：

1. **主频相符**：`BM_CONFIG_CPU_FREQ_HZ` 必须等于 `bm_hal_cpu_freq_hz()`
   的运行期真值；
2. **cfg 点集 ⊆ port 支持集**：`BM_CONFIG_CPU_DVFS_POINTS_HZ` 里声明的每
   个频率都必须在 `bm_hal_cpu_freq_points()` 返回的支持集合内；
3. **锚点 ∈ port 支持集**：`BM_CONFIG_CPU_FREQ_HZ` 本身也必须落在
   `bm_hal_cpu_freq_points()` 支持集合内（锚点必须是一个真实可用的工作点，
   不能是任意瞎填的数）。

`BM_CONFIG_CPU_FREQ_HZ==0`（未声明主频）时直接跳过、返回 `BM_OK`——不强
制每个工程都声明 config 主频。这个门面本身也**不强制调用**：应用或未来
的 PM 子系统可以在启动阶段可选调用一次，抓 config 与硬件运行期真值的漂移
（比如 fuse 烧写值与代码里写的 `BM_CONFIG_CPU_FREQ_HZ` 对不上）。

### 锚点语义（config 与 port 共同遵守）

整套频率相关的静态分析建立在一条约定上：任务声明的 `wcet_us` 是在
`BM_CONFIG_CPU_FREQ_HZ`（锚点频率 `ref_clk`）下测得/声明的，schedule-map
按 `wcet(f) = ceil(wcet_ref × ref/f)` 从这个锚点外推其它频率的估算峰值。
由此派生两条一致性规则（`include/bm_config.h` 的宏注释与本节均须遵守）：

- **`BM_CONFIG_CPU_FREQ_HZ` 必须是 `BM_CONFIG_CPU_DVFS_POINTS_HZ` 点集之
  一**——必须是你实际测过 wcet 的那个工作点；
- **声明了 `BM_CONFIG_CPU_DVFS_POINTS_HZ` 就必须声明 `BM_CONFIG_CPU_FREQ_HZ`**
  ——有点集无锚点，缩放换算找不到基准。schedule-map 工具遇到"有 DVFS 点但
  `ref_clk_hz==0`"时会退化为单表，并给出明确告警（区别于"未声明参考时钟"
  的泛化告警）。

### 边界：本版只做移植层原语

`bm_hal_cpu_freq_*` 这一版**只提供机制**——声明支持的频率点、查询当前
频率、切频占位——**不含 PM（电源管理）策略**。"何时切频""按什么策略选
档""省电模型""变频前后要不要通知依赖主频的模块（timer 重载值、UART
波特率、`bm_wcet_mon` 的 cycles↔µs 换算等）"，这些留给后续独立的 PM 子系统
讨论与实现；本版接口的签名与语义不排斥以后在此基础上接入。

## 移植（Port）与集成的关系

| 概念 | 内容 | 位置 |
|------|------|------|
| **库** | 事件、HAL 分发层、混合域 | `Source/`、`include/` |
| **Port** | `bm_drv_*_api` 实现 | [`portable/template/bm_port.c`](../portable/template/bm_port.c)，[03-Port移植层](03-Port移植层bm_port.md) |
| **集成** | 库怎么进 Keil/IAR/CMake | [02-挂库到现有工程](02-挂库到现有工程.md) |

本文描述 **Port 要实现什么**；挂库步骤见 [02-挂库](02-挂库到现有工程.md)。

## 相关集成文档

| 文档 | 内容 |
|------|------|
| [02-挂库到现有工程](02-挂库到现有工程.md) | 两步挂库总览 |
| [04-STM32-CubeMX集成](04-STM32-CubeMX集成.md) | Cube 工程 |
| [10-STM32G4-Port实机](10-STM32G4-Port实机.md) | sdk_stm32g4 + ATK-DMG474 冒烟验收 |
| [05-NXP-MCUXpresso集成](05-NXP-MCUXpresso集成.md) | MCUX 工程 |
| [06-Keil-MDK集成](06-Keil-MDK集成.md) / [07-IAR-EWARM集成](07-IAR-EWARM集成.md) | IDE 手工集成 |
| [08-ESP-IDF与灯哥平衡车集成](08-ESP-IDF与灯哥平衡车集成.md) | ESP-IDF + 灯哥板（无需 `bm_port.c`） |
