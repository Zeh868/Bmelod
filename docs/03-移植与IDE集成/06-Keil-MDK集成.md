# 18 Keil-MDK集成

> **本文职责**：在 Keil MDK-ARM 中添加 Bmelod 源文件、头文件路径与 Port。  
> **集成总览** → [02-挂库到现有工程](02-挂库到现有工程.md)。

## 1. 模型

与 FreeRTOS 相同：**Port 在应用工程**，库以源码或 `.lib` 形式加入。

1. **Port** — 复制 `portable/template/bm_port.c`，对接厂商 HAL。
2. **库** — 添加 `Source/` 文件（源码模式）或链接 `libbm_*.a`（[../02-构建与工具链/02-预编译静态库](../02-构建与工具链/02-预编译静态库.md)）。

## 2. Include 路径（顺序重要）

1. 应用目录（含 `bm_config.h`，**最先**）
2. 框架 `include/`（对外 API 在根目录）
3. 编写 Port 时再加 `include/bm/*`、`include/hal`、`include/drv`（见 `list_sources.py --list-includes`）
4. 厂商 CMSIS / 设备头文件

应用：`#include "bmelod.h"`、`#include "hal/bm_hal_uart.h"`。详见 [../02-构建与工具链/03-bmelod头文件与include](../02-构建与工具链/03-bmelod头文件与include.md)。

## 3. 库源文件列表

```bash
python tools/list_sources.py --profile event --format keil --root-macro BMELOD_ROOT
python tools/list_sources.py --profile event --list-includes --format keil --root-macro BMELOD_ROOT
```

将输出加入工程；**另加**应用侧 `bm_port.c`。静态库模式仍须在应用中编译 Port。

## 4. HAL / Port

在 `bm_port.c` 中实现 `bm_drv_*_api`。参考：`portable/vendor/stm32g4/`（`packs/sdk_stm32g4`）。

CubeMX 工程要点见 [04-STM32-CubeMX集成](04-STM32-CubeMX集成.md)。
