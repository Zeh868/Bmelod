# 05 CMake选项与bm_config

> **本文职责**：CMake 选项、`bm_config.h`、链接目标与 include 路径。  
> **不负责**：运行时行为与 `main` 顺序 → [02-main](../01-应用开发/02-main骨架与数据流.md)。

## 顶层 CMake 选项

在框架根目录 `CMakeLists.txt` 中通过 `option` 裁剪组件：

| 选项 | 默认 | 说明 |
|------|------|------|
| `BM_BUILD_TESTS` | OFF | 构建 `tests/unit` |
| `BM_ENABLE_MODULE` | ON | `bm_module` |
| `BM_ENABLE_SHELL` | OFF | `bm_shell` |
| `BM_ENABLE_WDG` | ON | `bm_wdg` |
| `BM_ENABLE_HRT` | OFF | `bm_hrt` |
| `BM_ENABLE_TICKER` | OFF | `bm_ticker` |
| `BM_ENABLE_EXEC` | OFF | `bm_exec`、`bm_resource`（依赖 HRT） |
| `BM_ENABLE_SYNC` | OFF | `bm_sync`（依赖 EXEC） |
| `BM_ENABLE_STREAM` | OFF | `bm_stream`（可独立于 EXEC 启用） |
| `BM_ENABLE_PIPELINE` | OFF | `bm_pipeline` 静态线性链（依赖 STREAM 或 EXEC） |
| `BM_ENABLE_ALGORITHM` | OFF | `bm_algorithm` 纯数学核（仅 `bm_config` + libm） |
| `BM_SYNC_HAL_NATIVE` | — | 测试用 native 同步 HAL |

依赖链：

```text
BM_ENABLE_SYNC → BM_ENABLE_EXEC → BM_ENABLE_HRT
BM_ENABLE_PIPELINE → BM_ENABLE_STREAM（或 EXEC）
```

## CMake 目标

| 目标 | 内容 |
|------|------|
| `bm_core` | 事件、内存池、临界区 |
| `bm_module` / `bm_shell` / `bm_wdg` | 可选组件 |
| `bm_hrt` / `bm_ticker` / `bm_exec` / `bm_resource` / `bm_sync` | 混合域 |
| `bm_stream` | 静态零拷贝块流（可选，测试默认 ON） |
| `bm_pipeline` | 编译期线性处理链（可选，测试默认 ON） |
| `bm_algorithm` | 纯算法库（可选，测试默认 ON） |
| `bm_hal` | 弱符号默认桩 |
| `bm_hal_native` / `bm_hal_stm32g4` / `bm_hal_esp32wroom32e` / … | 平台强符号覆盖 |
| `bm_framework` | 已启用组件的聚合接口库 |

应用应**只链接用到的目标**；需要全开时用 `bm_framework`。

## 应用工程集成

Bmelod 是库：**先移植 Port，再集成**（源码或静态库）。见 [../02-构建与工具链/02-挂库到现有工程](../02-构建与工具链/02-挂库到现有工程.md)。

| 方式 | 入口 |
|------|------|
| 源码（CMake） | `cmake/bmelod.cmake` |
| 源码（Keil/IAR） | `tools/list_sources.py` + `portable/template/bm_port.c` |
| 静态库 | `cmake/static-lib/` |

```cmake
bmelod_configure(ROOT ... PROFILE event BACKEND external CONFIG bm_config.h)
target_sources(my_app PRIVATE bm_port.c)
bmelod_link(my_app)
```

## 底层 add_subdirectory（高级）

仍可直接 `add_subdirectory(bmelod-baremetal)` 并手动设置 `BM_ENABLE_*`、`BM_BACKEND`。框架内示例与单元测试采用此方式。

## `bm_config.h`

所有上限在编译期固定。在应用 include 路径放置 `bm_config.h`，或通过 CMake 设置 `BM_CONFIG_FILE`。

应用 API 入口：`#include "bmelod.h"`。Include Path 只需框架 `include/` 一条（见 [../02-构建与工具链/03-bmelod头文件与include](../02-构建与工具链/03-bmelod头文件与include.md)）。

CMake 通过 `bm_config` 目标将 `BM_ENABLE_*` 同步为 `BM_CONFIG_ENABLE_*`；容量宏仍在应用 `bm_config.h` 中定义。

组件开关示例：

```c
#define BM_CONFIG_ENABLE_MODULE             1
#define BM_CONFIG_ENABLE_HRT                0   /* Control 层再置 1 */
#define BM_CONFIG_ENABLE_ALGORITHM          0   /* 算法库，与 bmelod.h 独立 */
#define BM_CONFIG_ENABLE_ULTRA              0   /* Ultra 剖面置 1 */
```

常用容量宏：

```c
/* 事件 */
#define BM_CONFIG_MAX_EVENT_TYPES           16
#define BM_CONFIG_MAX_EVENT_SUBSCRIBERS     32
#define BM_CONFIG_EVENT_QUEUE_SIZE          16
#define BM_CONFIG_EVENT_PRIORITIES          4
#define BM_CONFIG_EVENT_PRIORITY_BURST_MAX   8

/* 模块 / Shell / 看门狗 */
#define BM_CONFIG_MAX_MODULES                8
#define BM_CONFIG_SHELL_BUF_SIZE            64
#define BM_CONFIG_SHELL_MAX_ARGS             4
#define BM_CONFIG_SHELL_MAX_NAME_LEN         16
#define BM_CONFIG_WDG_MODULE_TIMEOUT_MS   1000

/* 混合域 */
#define BM_CONFIG_HRT_TICK_US                100
#define BM_CONFIG_HRT_MAX_SLOTS              16
#define BM_CONFIG_MAX_EXEC_INSTANCES         16
#define BM_CONFIG_MAX_RESOURCE_CLAIMS        64
#define BM_CONFIG_MAX_SYNC_MEMBERS           BM_CONFIG_MAX_EXEC_INSTANCES
#define BM_CONFIG_SYNC_MAX_PHASE_TICKS       1000000000u
```

模板见仓库根 `bm_config.h.template`。CMake 会 **force-include** 该头，保证框架与应用宏一致。

也可用编译器 `-D` 覆盖单个宏。

## Makefile 路径

8 位或极简工具链可用根目录 `Makefile`，不依赖 CMake。见 `Demo/` 中部分 Makefile 示例。

## 8 位 Ultra

不链接 `Source/`，仅 `#include "bm_ultra.h"`。见 `Demo/ultra_blink`。

## 常见问题

| 现象 | 处理 |
|------|------|
| 链接到弱符号 HAL，外设无反应 | 链接对应 `bm_hal_<platform>` |
| `BM_ENABLE_EXEC` 配置失败 | 同时打开 `BM_ENABLE_HRT` |
| MSVC 与头文件换行 | 头文件保持 LF，避免 C2143 |
