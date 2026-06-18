# Bmelod Baremetal

面向资源受限 MCU 的可裁剪确定性事件、控制与流式计算框架。

当前稳定的是面向电机驱动、数字电源、BMS 和机器人末端节点的确定性执行底座；**公共纯算法库 `bm_algorithm`（K0，`E1`）已交付**，领域组件（有感 FOC、电源/BMS 编排等）仍在路线图阶段。架构路线将同一套静态资源、确定性执行和故障隔离能力扩展到音频 DSP、振动诊断、工业测量、传感融合、超声/低速雷达和低分辨率视觉。它为从 8 位微控制器到中等规模 ARM Cortex-M 提供统一的编程模型，并在硬件资源充裕时，提供清晰的向 [Bmelod on Zephyr](https://github.com/Bmelod/Bmelod) 的迁移路径。

> 未在仓库中合入的组件不会在本文中作为现成功能承诺。使用说明见 [docs/README.md](docs/README.md)。

---

## 亮点

- **资源分级可裁剪**：从 `<8 KB Flash / <1 KB RAM`（头文件库 `bm-ultra`）到 `~12 KB Flash / ~4 KB RAM`（完整混合域控制）。
- **零堆内存设计**：所有内存在编译期或早期 `init()` 中静态分配。不使用 `malloc` / `free`。
- **混合域执行**：硬实时（HRT）控制回路与软实时（SRT）事件驱动业务逻辑严格隔离。
- **多实例执行**：同时运行多个控制、采集或估算实例，支持显式资源声明与冲突检测。
- **同步域**：对多路 PWM/ADC 进行相位锁定，适用于交错电源拓扑或同步机器人关节。
- **跨域数据交换**：无锁三缓冲快照（`bm_snapshot`），实现 HRT 到 SRT 的安全数据传递。
- **故障安全启动**：`bm_exec_init_all()` 在执行前先校验所有实例、资源和绑定关系；任何失败都会触发有序回滚并进入安全停机状态。
- **纯算法库**：`bm_algorithm` 提供 PI/滤波/FOC 数学核、FFT、融合等 K0 API（`E1`，float32），与 `bm_core` 无依赖。
- **块流**：`bm_stream` 静态零拷贝块流已合入，可与 `bm_exec` Block/Frame 槽及 `bm_pipeline` 配合使用（见 [05-混合域接线](docs/01-应用开发/05-混合域接线.md)）。

---

## 架构

```text
┌─────────────────────────────────────────────────────────────────────┐
│  应用层                                                              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌───────────┐ │
│  │  bm_module  │  │ bm_channel  │  │  bm_shell   │  │  bm_wdg   │ │
│  │ (模块生命周期) │  │  (SPSC通道)  │  │  (命令行)   │  │ (看门狗)   │ │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └─────┬─────┘ │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │                        bm_core                                  │ │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────────────┐  │ │
│  │  │ bm_event │  │bm_mempool│  │bm_critical│  │   bm_types     │  │ │
│  │  │(发布/订阅) │  │ (内存池)  │  │ (临界区)  │  │  (错误码体系)   │  │ │
│  │  └──────────┘  └──────────┘  └──────────┘  └────────────────┘  │ │
│  └────────────────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │  混合域（可选）                                                    │ │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ │ │
│  │  │   bm_hrt    │  │  bm_ticker  │  │      bm_exec       │ │ │
│  │  │(定时调度 HRT)│  │ (SRT 周期任务)│  │ (通用执行实例 + 资源声明) │ │ │
│  │  └─────────────┘  └─────────────┘  └─────────────────────────┘ │ │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ │ │
│  │  │bm_snapshot  │  │   bm_sync   │  │    HAL 接口契约          │ │ │
│  │  │(跨域邮箱)   │  │ (相位同步)   │  │  (PWM/ADC/比较器/编码器)  │ │ │
│  │  └─────────────┘  └─────────────┘  └─────────────────────────┘ │ │
│  └────────────────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │  算法库（可选，独立于 bm_core）                                  │ │
│  │  bm_algorithm — PI/滤波/电机数学核/FFT/融合/音频/图像…（E1）      │ │
│  └────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
```

### 执行域

| 执行域 | 触发方式 | 典型频率 | 适用任务 |
|--------|---------|---------|---------|
| **Hardware HRT** | PWM / ADC / 比较器硬件中断 | 5–200 kHz | 电流环、数字电源、硬件故障保护 |
| **Scheduled HRT** | 专用定时器中断 | 100 Hz – 10 kHz | 速度环、低频采样、确定性派生环 |
| **SRT** | 主循环轮询 + 事件队列 | 无硬截止期限 | 状态机、通信、诊断、命令行 |

**核心规则**：Hardware HRT 与 Scheduled HRT 绝不访问 SRT 的事件队列或受临界区保护的数据结构。这保证了高优先级控制回路不会被业务逻辑处理延迟。

规划中的 **Block/Frame RT** 由 DMA half/full 或 frame-ready 触发；`bm_stream`
已提供静态块流 API，与 `bm_exec` block slot 的深度集成仍在演进。

---

## 资源层级

| 层级 | Flash | RAM | 典型 MCU | 包含组件 |
|------|-------|-----|---------|---------|
| **Ultra** | < 8 KB | < 1 KB | STM8、AVR、8051 | `BM_CONFIG_ENABLE_ULTRA` / `bm_ultra.h` |
| **Nano** | 8–32 KB | 1–4 KB | CH32V003、STM32F030 | `bmelod.h` 或 `bm_lite.h` |
| **Lite** | 32–128 KB | 4–16 KB | STM32F103、nRF51822、ESP32-WROOM-32E | + channel / shell（`BM_CONFIG_ENABLE_*`） |
| **Control** | 32–128 KB+ | 4–16 KB+ | STM32G4、STM32F3 | + HRT / `bm_exec` / sync（`bm_hybrid.h`） |
| **DSP** | 64–256 KB | 16–128 KB | Cortex-M4F/M7、ESP32-S3 | + `bm_algorithm`、`bm_stream` |
| **Media Edge（规划）** | 128 KB+ | 64 KB+ / 外部 RAM | Cortex-M7、带 PSRAM MCU | + 完整 pipeline、camera/audio HAL |

---

## 覆盖领域

Bmelod Baremetal 面向**资源受限 MCU 上的确定性控制与流式计算节点**。以下
同时标注现有能力与规划能力。

这里的“覆盖”指承担行业设备中的实时 MCU 子系统，不等于提供完整 PLC、
工业协议栈、SLAM、医疗诊断、功能安全认证或高分辨率媒体系统。

### ★★★★★ 原生匹配 — 核心设计目标

| 领域 | 典型应用场景 | 匹配原因 | 状态 |
|------|------------|---------|------|
| **数字电源** | DC-DC、PFC、LLC、多相 VRM | Hardware HRT + Scheduled HRT + 同步域 | 现有底座 |
| **电池管理（BMS）** | 电芯采样、库仑计数、均衡控制 | 多实例、ADC/DMA、snapshot、保护 | 现有底座 |
| **伺服/运动控制** | 工业伺服、机器人关节、步进驱动 | 电流/速度/位置多速率环 | 现有底座 |
| **机器人** | 机械臂、移动机器人、末端执行器 | 多轴实例、同步、事件协调 | 现有底座 |
| **声学/振动诊断** | 异音、轴承、结构健康 | DMA 块 + FFT/包络/特征 | R1–R3 规划 |
| **工业测量/计量** | 高速采集、功率质量、记录仪 | 同步采样块 + 统计/频谱 | R1–R3 规划 |
| **传感融合/感知** | IMU/AHRS、超声、低速雷达 | 时间戳、周期融合、相关/FFT | R2–R3 规划 |
| **移动与作业机械低层** | AGV、农机、液压、输送、3D 打印 | 运动/过程闭环、融合、联锁 | 算法组件规划 |
| **过程与楼宇节点** | HVAC、灌溉、门机、半导体辅助控制 | 多速率控制、顺序状态机、故障降级 | 算法组件规划 |

### ★★★★☆ 高度适配 — 少量缺口（通信协议栈）

| 领域 | 典型应用场景 | 需补齐的缺口 |
|------|------------|------------|
| **汽车电子（QM 级）** | BCM、热管理、灯光、传感器节点 | 需集成 CAN/LIN/UDS 协议栈 |
| **无人机/飞行器** | 电调、云台电机、舵机驱动 | 需无感 FOC 算法和 DShot/OneShot 协议 |
| **消费电子/白色家电** | 变频空调、洗衣机、吸尘器 | 需显示/触摸和无线连接协议栈 |
| **能源/光伏/储能** | 光伏逆变器、PCS、充电桩模块 | MPPT/PLL **数学核已有**；需 OCPP/IEC 61850 协议与领域组件 |
| **物联网/传感器节点** | 环境监测、智能表计、农业物联网 | 需 LoRa/BLE/Zigbee 和深度低功耗策略 |
| **嵌入式音频** | 对讲前处理、提示音、EQ、AGC、VAD | 需 stream/pipeline 与 I2S/SAI/PDM/DAC HAL |
| **低分辨率视觉** | 阈值、形态学、码/线检测、帧差 | 需 frame stream、DCMI/CSI、cache/外部 RAM |
| **生物信号** | ECG/PPG/EMG 前处理 | 需产品级医学算法与认证证据 |
| **通信 DSP / TinyML** | DTMF/FSK、特征、异常检测 | 协议栈/推理内核建议外置 |
| **PLC/网关/实时以太网适配** | 过程镜像、报文整形、确定性 QoS | 语言 runtime 和认证协议栈外置 |

### ★★★☆☆ 有限适配 — 可行但需额外投入

| 领域 | 典型应用场景 | 需补齐的缺口 |
|------|------------|------------|
| **工业自动化** | 远程 IO、步进驱动、温控 PID | 需 Modbus/工业以太网协议栈 |
| **医疗设备** | 输注泵、呼吸机电机、电动病床 | 需 IEC 62304 流程和安全认证（框架本身未认证） |
| **智能照明** | LED 驱动、DALI/DMX 解码器 | 需 DALI/DMX 和无线 mesh 协议 |

> **不适用场景**：商业航空（DO-178C）、高安全等级汽车（ASIL-D）、复杂
> GUI/HMI、高分辨率软件视频编解码、大型神经语音/视觉模型、完整 5G 基带、
> 植入式医疗和 SIL4 轨交整机控制。

---

## 目录结构

```text
bmelod-baremetal/
├── include/              # 公共 API（见 include/README.md）
│   ├── bmelod.h          # 统一对外入口
│   ├── bm_*.h            # 分层聚合头 + bm_hal.h
│   ├── bm/common|core|hybrid|algorithm/  # 框架与算法头
│   ├── hal/              # bm_hal_* 契约
│   └── drv/              # bm_drv_*（Port）
├── Source/               # 库内核（类比 FreeRTOS/Source/）
│   ├── core/             # 事件、内存池、模块、看门狗…
│   ├── hal/              # HAL 分发层
│   ├── hybrid/           # HRT、bm_exec、sync、stream…
│   └── algorithm/        # bm_algorithm 纯数学核
├── portable/             # 平台 Port（类比 FreeRTOS/portable/）
│   ├── packs/            # BM_BACKEND 组合包（native_sim、sdk_stm32g4…）
│   ├── sim/              # 仿真后端（native、qemu_cm0、qemu_riscv64）
│   ├── vendor/           # 芯片外设（stm32g4、ch32v003）
│   └── arch/             # ISA 临界区与屏障
├── Demo/                 # 示例（类比 FreeRTOS/Demo/）
├── tests/
│   ├── unit/             # Unity 单元测试（PC 本地运行）
│   └── qemu/             # QEMU 冒烟测试
├── docs/
│   ├── README.md         # 文档索引（00–21）
│   ├── 00-环境与首次运行.md … 04-测试覆盖率基线.md
│   └── api/              # API 参考（唯一子目录）
├── CMakeLists.txt        # CMake 构建（32 位主流平台）
└── Makefile              # 纯 Makefile（8 位工具链友好）
```

---

## 示例

[`Demo/`](Demo/) 目录包含逐步扩展的演示程序（类比 FreeRTOS/Demo/）：

| 示例 | 重点 | 层级 | 成熟度 |
|---------|-------|------|--------|
| [`ultra_blink`](Demo/ultra_blink) | 最小化的头文件版事件队列 | Ultra | `D0` |
| [`core_sensor`](Demo/core_sensor) | 事件、内存池与模块生命周期 | Nano | `D0` |
| [`full_system`](Demo/full_system) | 多模块、事件优先级、看门狗 | Lite | `D0` |
| [`interrupt_demo`](Demo/interrupt_demo) | SysTick、外设中断与 ISR 事件发布 | Nano | `D0` |
| [`hrt_servo_stub`](Demo/hrt_servo_stub) | 混合域伺服 | Control | `D0` |
| [`hrt_bms_coulomb`](Demo/hrt_bms_coulomb) | BMS 混合域 | Control | `D0` |
| [`multi_axis_sync`](Demo/multi_axis_sync) | 同步域多轴 | Control | `D0` |
| [`multi_channel_bms`](Demo/multi_channel_bms) | 多通道 BMS | Control | `D0` |

`D0` 表示机制演示，不代表相关领域算法达到产品或工业成熟度。示例说明见
[01-Demo示例与运行路径](docs/01-应用开发/01-Demo示例与运行路径.md)。

构建说明见 [`Demo/README.md`](Demo/README.md)。

### 集成到已有工程（类 FreeRTOS）

**① 移植** `portable/template/bm_port.c` → 接 Cube/SDK HAL  
**② 库集成** `Source/` 源码或 `libbm_*.a`（见 `cmake/static-lib/`）

```bash
# 源码集成：库文件列表（不含 Port）
python tools/list_sources.py --profile event --format keil
```

```cmake
# CMake 源码集成
bmelod_configure(ROOT ... PROFILE event BACKEND external CONFIG bm_config.h)
target_sources(app PRIVATE Core/Src/bm_port.c)
bmelod_link(app)
```

```c
/* 应用侧：Include Path 只需框架 include/ 一条 */
#include "bmelod.h"
#include "bm_algorithm.h"  /* 可选：控制/DSP 纯算法 */
#include "bm_hal_uart.h"   /* 板级 HAL 按需 */
```

详见 [`docs/03-移植与IDE集成/02-挂库到现有工程.md`](docs/03-移植与IDE集成/02-挂库到现有工程.md)、[`docs/01-应用开发/03-bmelod头文件与include.md`](docs/01-应用开发/03-bmelod头文件与include.md)。

### 快速开始（本地模拟）

```bash
cmake -B build/demo/manual/core_sensor -S Demo/core_sensor -DBMELOD_BAREMETAL_DIR=../..
cmake --build build/demo/manual/core_sensor
# 或：.\tools\demo\run_native.ps1 core_sensor
```

### 快速开始（QEMU Cortex-M0）

```bash
.\tools\demo\run_qemu.ps1 interrupt_demo
```

---

## 测试

| 层级 | 目标 | 工具 | 运行环境 |
|-------|--------|------|-------------|
| **单元测试** | 算法、数据结构、状态机 | Unity + fff（mock） | PC（`native_sim`） |
| **QEMU 冒烟** | HAL 集成、中断上下文、启动流程 | 自定义 TAP 输出测试框架 | QEMU Cortex-M0 / RISC-V |
| **硬件在环** | 真实时序、功耗、外设 | 手动 + 自动化脚本 | 参考开发板 |

运行单元测试：

```bash
cmake -B build -DBM_BUILD_TESTS=ON
cmake --build build
cd build && ctest
```

---

## 配置

所有框架限制均可在编译期通过 `bm_config.h`（由 CMake 强制包含）或 `-D` 编译器标志进行配置：

```c
/* 核心事件系统 */
#define BM_CONFIG_MAX_EVENT_TYPES           16
#define BM_CONFIG_MAX_EVENT_SUBSCRIBERS     32
#define BM_CONFIG_EVENT_QUEUE_SIZE          16
#define BM_CONFIG_EVENT_PRIORITIES          4

/* 可选组件 */
#define BM_CONFIG_MAX_MODULES                8
#define BM_CONFIG_SHELL_BUF_SIZE            64
#define BM_CONFIG_MAX_WDG_MODULES            4

/* 混合域 */
#define BM_CONFIG_HRT_TICK_US                100
#define BM_CONFIG_HRT_MAX_SLOTS              16
#define BM_CONFIG_MAX_EXEC_INSTANCES         16
#define BM_CONFIG_MAX_RESOURCE_CLAIMS        64
```

将 `bm_config.h` 放在应用层的 include 路径中（须优先于框架 `include/`），或在 CMake 中设置 `BM_CONFIG_FILE`。

组件开关（与 CMake `BM_ENABLE_*` 对齐）：

```c
#define BM_CONFIG_ENABLE_MODULE             1
#define BM_CONFIG_ENABLE_HRT                0   /* Control 层再置 1 */
#define BM_CONFIG_ENABLE_ALGORITHM          0   /* 算法库，CMake: BM_ENABLE_ALGORITHM */
#define BM_CONFIG_ENABLE_ULTRA              0   /* Ultra 剖面置 1 */
```

应用 API 入口：`#include "bmelod.h"`。Include Path 只需 `bmelod-baremetal/include/` 一条。

---

## 迁移路径

Bmelod Baremetal 被设计为机器人/电子系统三层架构的底层：

```text
[Ultra]  --升级芯片-->  [Core]  --需要模块化-->  [Core + Module]
                                                         |
                                                         v
                                             [Zephyr + Bmelod 完整版]
```

- **Ultra → Core**：编译期事件绑定改为运行时订阅；引入内存池；获得优先级和多订阅者能力。
- **Core → Core+Module**：分散的中断/主循环逻辑封装为带生命周期管理的模块。
- **Baremetal → Zephyr**：模块代码基本可复用；事件 API 语义对齐。`bm-channel` 必须替换为 Bmelod Data Bus。

详细指南见 [02-版本迁移与演进](docs/04-测试与排障/02-版本迁移与演进.md)。

---

## 文档

**导读（推荐从此开始）**：[`docs/README.md`](docs/README.md)

完整索引与三条推荐阅读路径见 [`docs/README.md`](docs/README.md)。主要章节：

| 章节 | 文档 |
|------|------|
| 00 入门 | [环境与首次运行](docs/00-入门与快速开始/00-环境与首次运行.md) |
| 01 应用开发 | [Demo](docs/01-应用开发/01-Demo示例与运行路径.md) · [main 骨架](docs/01-应用开发/02-main骨架与数据流.md) · [头文件](docs/01-应用开发/03-bmelod头文件与include.md) · [事件](docs/01-应用开发/04-事件模块与通道.md) · [混合域](docs/01-应用开发/05-混合域接线.md) |
| 02 构建 | [CMake 与 bm_config](docs/02-构建与工具链/01-CMake选项与bm_config.md) · [静态库](docs/02-构建与工具链/02-预编译静态库.md) |
| 03 移植 | [HAL](docs/03-移植与IDE集成/01-HAL契约与移植要点.md) · [挂库](docs/03-移植与IDE集成/02-挂库到现有工程.md) · [Port](docs/03-移植与IDE集成/03-Port移植层bm_port.md) · IDE 集成（CubeMX / MCUX / Keil / IAR） |
| 04 排障 | [单元测试](docs/04-测试与排障/01-单元测试与排障.md) · [迁移](docs/04-测试与排障/02-版本迁移与演进.md) · [运行时约束](docs/04-测试与排障/03-运行时约束与排障.md) |
| 05 API | [docs/05-API参考/](docs/05-API参考/)（含 [bm_algorithm](docs/05-API参考/bm_algorithm.md)） |

---

## 设计原则

1. **静态优先**：优先使用编译期分配和基于宏的注册，而非运行时动态发现。
2. **显式契约**：每一条资源声明、调度槽位和跨域数据路径都必须显式声明。
3. **有界执行**：所有循环、队列和表格均有静态已知的最大尺寸；禁止无界递归。
4. **可测试性**：每个组件都可通过 `native_sim` 在 PC 上测试；QEMU 在接触硬件前提供架构级冒烟测试。
5. **故障安全**：初始化路径在提交硬件状态前先完成全部校验；失败时触发确定性回滚。

---

## 参与贡献

欢迎贡献。在添加新组件或 HAL 参考实现时，请遵循：

- 保持零堆内存原则。
- 提供 `native_sim` 实现和单元测试。
- 更新 `docs/05-API参考/` 中的相关 API 参考文档。
- 如该功能面向终端用户，请新增或扩展示例。

---

## 许可证

Bmelod Baremetal 以 [GNU Lesser General Public License v3.0](COPYING.LESSER)（LGPL-3.0）发布；其并入的 GPLv3 完整条款见 [COPYING.GPL3](COPYING.GPL3)。版权与免责声明见 [NOTICE](NOTICE)（`SPDX-License-Identifier: LGPL-3.0-or-later`）。

作为库静态链接进专有固件时，须遵守 LGPL-3.0 对**库部分**的再链接与源码/目标文件提供义务；应用层专有代码通常不受 GPL 传染性约束。裸机合规细节请以法务意见为准。

第三方组件可能使用其他许可证，见其文件头（例如 `tests/unit/unity/` 中的 Unity 为 MIT）。

---

*Bmelod Baremetal 是 Bmelod 机器人软件栈的一部分。它不是通用 RTOS 的替代品，而是面向资源受限机电控制节点的专用框架。*
