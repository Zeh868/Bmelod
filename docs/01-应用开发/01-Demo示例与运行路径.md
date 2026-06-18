# 06 Demo示例与运行路径

> **本文职责**：`Demo/` 示例一览、推荐学习顺序、运行命令与硬件移植要点。  
> **数据流与 `main` 顺序** → [02-main骨架与数据流](02-main骨架与数据流.md)。

`Demo/` 由浅入深；各示例自带 `bm_config_app.h`（应用容量覆盖，由 CMake `-include` 注入）与 `CMakeLists.txt`。应用侧推荐 `#include "bmelod.h"`（`full_system` 已采用）；HAL 仍按需单独包含。

**机制接线** → [03](../01-应用开发/05-混合域接线.md)、[04](../01-应用开发/05-混合域接线.md)  
**头文件入口** → [03-bmelod头文件与include](03-bmelod头文件与include.md)  
**混合域 API 速查** → [api/](api/)（`bm_exec`、`bm_hrt`、`bm_sync` 等）

Control 级示例的 `main` 均遵循 [02-main §5.2](02-main骨架与数据流.md#52-正确初始化顺序)：`init_all` →（可选 sync configure/arm）→ `start_all` →（可选 sync trigger）→ `ticker_init`。

## 1. 示例一览

| 示例 | 层级 | 成熟度 | 学习重点 |
|------|------|--------|----------|
| `ultra_blink` | Ultra | `D0` | 头文件事件队列 |
| `core_sensor` | Nano | `D0` | 事件 + 池 + `bm_module` |
| `interrupt_demo` | Nano | `D0` | 板级 ISR → 事件入队 |
| `full_system` | Lite | `D0` | 多模块、看门狗、Shell |
| `hrt_servo_stub` | Control | `D0` | 三环混合域 |
| `hrt_bms_coulomb` | Control | `D0` | snapshot + 库仑积分 |
| `closed_loop_servo` | Control | `E1` | bm_algorithm + 速度环闭环 + snapshot |
| `foc_current_loop` | Control | `E1` | Clarke/Park + dq 电流环 + SVPWM + snapshot |
| `motor_foc_sensored` | Control | `E1` | `bm_component_motor` 双环 FOC + 编码器 |
| `stream_block_rms` | DSP | `E1` | `bm_stream` + Block 槽块级 RMS |
| `stream_fft` | DSP | `E1` | DMA 块流 + RFFT 频谱峰 |
| `stream_bms_pipeline` | DSP/BMS | `E1` | `bm_stream` + `bm_pipeline` 线性链 + 库仑 SOC |
| `multi_axis_sync` | Control | `D0` | `bm_sync` |
| `multi_channel_bms` | Control | `D0` | 多路 snapshot |

这里的 `D0` 表示机制演示：示例验证框架接线和执行路径，不表示伺服、FOC、
BMS 或同步控制算法可用于产品。算法成熟度定义与晋级门槛见
本文档 §1 示例一览中的成熟度列。

## 2. 推荐顺序

```text
ultra_blink → core_sensor → interrupt_demo → full_system
                              ↓
            hrt_servo_stub → closed_loop_servo → foc_current_loop → motor_foc_sensored
                              ↓
            multi_axis_sync / hrt_bms_coulomb
                              ↓
            stream_block_rms → stream_fft → stream_bms_pipeline（块流 + pipeline + BMS）
```

## 3. 运行

脚本在 **`tools/demo/`**；产物统一写入仓库根目录 **`build/demo/<variant>/<example>/`**，不进入各示例目录。路径定义见 `tools/demo/demo_paths.sh` / `demo_paths.ps1`。

| variant | 说明 |
|---------|------|
| `native` | `run_native.ps1`（PC 模拟） |
| `qemu` | `run_qemu.*`（CI 冒烟） |
| `unix` / `windows` | `run_all.*`、`run_single.*` |
| `manual` | 下文手工 `cmake -B` |
| `make` | Makefile 示例（`qemu_example.mk`） |
| `board` | 板级工程（如 `motor_foc_sensored/board/`） |

```powershell
.\tools\demo\run_native.ps1 core_sensor
.\tools\demo\run_qemu.ps1 interrupt_demo
.\tools\demo\run_all.ps1
```

```bash
./tools/demo/run_qemu.sh hrt_servo_stub
./tools/demo/run_all.sh
```

手动构建（推荐统一输出目录）：

```bash
cmake -B build/demo/manual/core_sensor -S Demo/core_sensor -DBMELOD_BAREMETAL_DIR=../..
cmake --build build/demo/manual/core_sensor
```

## 4. 目录约定

```text
Demo/                         # 仅示例源码
  common/                     # 共享轻量输出工具
  cmake/                      # QEMU / native_sim 共享 CMake 逻辑
  <example>/                  # main.c、bm_config_app.h、CMakeLists.txt
    modules/                  # BM_MODULE_DEFINE + module_table.c（含模块的示例）
tools/demo/                   # run_*.sh/ps1、demo_paths.*、examples.txt
build/demo/<variant>/<name>/  # 所有示例构建产物（gitignore，见仓库根 build/）
```

## 5. 编码与配置约定

- 示例目录使用 **`bm_config_app.h`** 存放容量覆盖宏；**勿**在应用目录放置名为 `bm_config.h` 的文件（会遮蔽框架 `include/bm_config.h` 默认值）。
- `bm_config_app.h` 由 CMake `BM_CONFIG_FILE` 强制 `-include`；文件头注释须为 **UTF-8**（建议带 BOM，便于 Windows 编辑器识别中文）。
- 寄存器/向量 ISR 放板级文件（如 `interrupt_demo/interrupt_timer.c`），不进 `main.c`。
- 异步消息用 `bm_event_publish_copy`；跨 HRT 数据用 `bm_snapshot`。

## 6. 移植到真实硬件

示例默认面向 QEMU / `native_sim`。移植到目标 MCU 时：

| 保留 | 替换 |
|------|------|
| `<example>/main.c`、`bm_config_app.h` | QEMU startup、链接脚本、板级 HAL |
| `common/example_support.c`（可选） | 目标板 UART / delay 实现 |
| `interrupt_demo/interrupt_timer.c` | 目标平台定时器 ISR 实现 |

各示例 `CMakeLists.txt` 或 `Makefile` 中的框架源选择为准；QEMU 共享策略在 `Demo/cmake/` 与 `tools/demo/qemu_example.mk`。

挂入厂商工程见 [../02-构建与工具链/02-挂库到现有工程](../02-构建与工具链/02-挂库到现有工程.md)；Keil / IAR 见 [06-Keil](../02-构建与工具链/06-Keil-MDK集成.md)、[07-IAR](../02-构建与工具链/07-IAR-EWARM集成.md)。
