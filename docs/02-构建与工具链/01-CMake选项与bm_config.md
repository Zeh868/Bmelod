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

/* bm_tt_schedule 诊断报告用（不影响调度语义，只影响 report/report_json 的“负载账”） */
#define BM_CONFIG_TT_SCHED_OVERHEAD_US        0u  /* 每 minor 格框架派发开销(µs)声明值，缺省 0，真机标定后回填 */
#define BM_CONFIG_TT_SCHED_OVERHEAD_CALIBRATED 0  /* 0=未标定占位（缺省）1=已实测标定，report_json 的 overhead_calibrated 字段直接取此值 */
```

模板见仓库根 `bm_config.h.template`。CMake 会 **force-include** 该头，保证框架与应用宏一致。

也可用编译器 `-D` 覆盖单个宏。

## `bm_add_schedule_map`：一行接入调度表导出

`bm_tt_schedule` 装配文件（见
[../01-应用开发/06-调度表导出schedule-map](../01-应用开发/06-调度表导出schedule-map.md)）
只需一行 CMake 函数即可在**构建结束后自动**导出 `.txt`/`.json` 并跑级 2
复合分析，函数定义在 `cmake/bm_schedule_map.cmake`：

```cmake
include(${BMELOD_ROOT}/cmake/bm_schedule_map.cmake)
bm_add_schedule_map(<name>
    SOURCES <装配文件.c...>
    SETUP   <setup 函数名>
    TABLES  <sched[:cpu]...>
    [INCLUDE_DIRS <目录...>]
    [REF_CLK_HZ <Hz>]
    [OPERATING_POINTS <Hz...>]
    [LINK_LIBS <目标...>]
    [OUTPUT_DIR <目录>])
```

| 参数 | 必填 | 说明 |
|------|------|------|
| `NAME`（位置参数） | 是 | 生成的 CMake 目标名（native 分支下是可执行目标；交叉编译分支下是 `add_custom_target`） |
| `SOURCES` | 是 | 装配文件（含 `BM_SCHEDULE_DEFINE`/`BM_LET_DEFINE_*` 的 `.c`），零硬件 include |
| `SETUP` | 是 | 装配文件里对外暴露的 `int xxx_setup(void)` 函数名，写进自动生成的注册单元 |
| `TABLES` | 是 | 调度表实例名列表，`sched:cpu` 形式声明所属 CPU（省略 `:cpu` 默认 0），如 `ctrl:0 bms:1` |
| `INCLUDE_DIRS` | 否 | 装配文件需要的额外 include 路径 |
| `REF_CLK_HZ` | 否 | 写入 JSON `ref_clk_hz`，默认 0（未声明，下游频率缩放分析会 WARN 跳过） |
| `OPERATING_POINTS` | 否 | 写入 JSON `operating_points_hz`，供级 2 工具做频率缩放估算 |
| `LINK_LIBS` | 否 | 覆盖默认链接目标（默认 `bm_tt_schedule bm_core bm_hal bm_hal_native`，按需加 `bm_wcet_mon` 等） |
| `OUTPUT_DIR` | 否 | 出表目录，默认 `${CMAKE_BINARY_DIR}/schedule_map` |

最小示例（`Demo/tt_schedule_balance` 的真实用法）：

```cmake
include(${BMELOD_ROOT}/cmake/bm_schedule_map.cmake)
bm_add_schedule_map(tt_schedule_balance_map
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/balance_schedule.c
    SETUP   balance_schedule_setup
    TABLES  ctrl:0
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR})
```

编译期若 `bm_tt_schedule_init` 失败（周期排不下、`wcet` 超载等），出表
程序返回非 0，POST_BUILD 步骤直接失败，构建不会带病通过。交叉编译
（`CMAKE_CROSSCOMPILING`）场景自动切到 `cmake/schedule_map_host/` 宿主
子构建；纯 Keil/IAR 无 CMake 主工程时改用
`tools/board/build_schedule_map.ps1`。三档细节见
[../01-应用开发/06-调度表导出schedule-map](../01-应用开发/06-调度表导出schedule-map.md)。

## 8 位 Ultra

不链接 `Source/`，仅 `#include "bm_ultra.h"`。见 `Demo/ultra_blink`。

## 常见问题

| 现象 | 处理 |
|------|------|
| 链接到弱符号 HAL，外设无反应 | 链接对应 `bm_hal_<platform>` |
| `BM_ENABLE_EXEC` 配置失败 | 同时打开 `BM_ENABLE_HRT` |
| MSVC 与头文件换行 | 头文件保持 LF，避免 C2143 |
