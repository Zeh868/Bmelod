# 08 ESP-IDF 与灯哥平衡车集成

> **本文职责**：在 ESP-IDF 工程中接入 Bmelod，面向灯哥平衡车主控板（ESP32-WROOM-32E）。  
> **通用挂库模型** → [02-挂库到现有工程](02-挂库到现有工程.md)；HAL 契约 → [01-HAL契约与移植要点](01-HAL契约与移植要点.md)。

---

## 1. 适用范围与非目标

| 项 | 说明 |
|----|------|
| **目标硬件** | 灯哥平衡车主控板，模组 **ESP32-WROOM-32E**（经典 ESP32，Xtensa LX6） |
| **工具链** | **ESP-IDF ≥ 5.0**（`idf.py` 工程） |
| **后端标识** | `BM_BACKEND=sdk_esp32_idf`，链接别名 `bm_hal_esp32wroom32e` |
| **非目标** | ESP32-S2/S3/C3/C6、Zephyr on ESP32、Arduino 核；本文不覆盖 |

**当前 HAL 能力（已实现）：**

| 驱动 API | 实现方式 | 说明 |
|----------|----------|------|
| `bm_drv_timer_api` | ESP-IDF `gptimer` | 系统 tick，1 MHz 分辨率 |
| `bm_drv_uart_api` | `CONFIG_ESP_CONSOLE_UART_NUM`（默认 UART0） | 不臆造 GPIO，沿用 IDF 控制台映射 |
| `bm_drv_wdg_api` | `esp_task_wdt` | Task WDT，兼容 IDF 5.x 配置 API |
| `bm_drv_critical_api` / `bm_drv_memory_api` | `portable/arch/xtensa/` | 由组件一并编译 |

**待补（需原理图 / 引脚表）：** 电机驱动、IMU、PWM、ADC、编码器等。见 `portable/vendor/esp32_idf/bm_hal_instances_esp32wroom32e.h`。

> **重要：** 仓库内**没有**可直接 `idf.py flash` 的完整平衡车应用工程；`Demo/` 示例均为 QEMU / `native_sim`（Cortex-M0 等）。灯哥板量产须自建 ESP-IDF 工程并按本文接入。

---

## 2. 架构：与 STM32 集成的差异

STM32 典型路径：复制 `portable/template/bm_port.c` → 在 Port 里调用 Cube `HAL_*`。

ESP32（灯哥板）路径：**无需应用侧 `bm_port.c`**。`portable/vendor/esp32_idf/` 已提供 vendor 单例，在 `ESP_PLATFORM` 下由 `idf_component_register` 编入工程。

```text
┌─────────────────────────────────────────────┐
│  你的 ESP-IDF 应用（main/、业务逻辑）         │
├─────────────────────────────────────────────┤
│  Bmelod 框架（Source/ + include/）           │
│  bmelod::framework / bm_framework             │
├─────────────────────────────────────────────┤
│  bm_hal_esp32wroom32e（INTERFACE）           │
│    ├─ bm_port_arch_xtensa                   │
│    └─ bm_vendor_esp32_idf（timer/uart/wdg） │
├─────────────────────────────────────────────┤
│  bm_config_app.h（-include 容量覆盖）        │
│  include/bm_config.h（框架默认宏）           │
└─────────────────────────────────────────────┘
         ESP-IDF（FreeRTOS、driver、链接脚本）
```

解析关系：`BM_BACKEND=sdk_esp32_idf` → `packs/sdk_esp32_idf` → `arch/xtensa` + `vendor/esp32_idf`。

源码入口：

- `portable/vendor/esp32_idf/bm_vendor_singleton_esp32_idf.c`
- `portable/vendor/esp32_idf/idf_component.yml`
- `portable/arch/xtensa/`（临界区、屏障）

---

## 3. 推荐路径：ESP-IDF 工程集成（量产）

### 3.1 环境

1. 安装 [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)。
2. 执行 `export.sh` / `export.ps1`，确认 `IDF_PATH`、`idf.py` 可用。
3. 将本仓库（`bmelod-baremetal`）放在已知路径。

### 3.2 推荐目录结构

```text
my_balance_car/                    # 你的 idf.py 工程根
├── CMakeLists.txt                 # 顶层 project()
├── sdkconfig                      # idf.py menuconfig 生成
├── main/
│   ├── CMakeLists.txt
│   ├── main.c                     # app_main → Bmelod 初始化与主循环
│   └── bm_config_app.h            # 应用容量覆盖（勿定义 BM_CONFIG_H）
└── （可选）components/ 下复制或链接 vendor 组件
```

### 3.3 方式 A：vendor 作为 ESP-IDF 组件（推荐）

**步骤 1 — 注册 Bmelod vendor 组件**

顶层 `CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(BMELOD_ROOT "/path/to/bmelod-baremetal" CACHE PATH "")

list(APPEND EXTRA_COMPONENT_DIRS
    "${BMELOD_ROOT}/portable/vendor/esp32_idf")

project(my_balance_car)
```

**步骤 2 — main 组件链入 Bmelod 框架**

`main/CMakeLists.txt`：

```cmake
set(BMELOD_ROOT "/path/to/bmelod-baremetal" CACHE PATH "")

set(BM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BM_BACKEND "sdk_esp32_idf" CACHE STRING "" FORCE)
set(BM_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/bm_config_app.h" CACHE FILEPATH "" FORCE)

if(NOT TARGET bm_core)
    add_subdirectory("${BMELOD_ROOT}" bmelod EXCLUDE_FROM_ALL)
endif()

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp32_idf
)

target_link_libraries(${COMPONENT_LIB} PRIVATE
    bm_framework
    bm_hal_esp32wroom32e
    bm_config
)
```

> `REQUIRES` 名称与 `esp32_idf` 目录名一致；若复制组件并重命名目录，须同步修改。

**步骤 3 — `bm_config_app.h`**

勿使用 `#ifndef BM_CONFIG_H` 包裹；由 `BM_CONFIG_FILE` 做 `-include` 预包含。示例见各 `Demo/*/bm_config_app.h`。

**步骤 4 — `app_main` 骨架**

```c
#include "bmelod.h"

void app_main(void)
{
    bm_event_reset();
    bm_module_init_all();
    for (;;) {
        bm_ticker_poll();
        bm_event_process();
        bm_wdg_feed();
    }
}
```

**步骤 5 — 构建与烧录**

```bash
idf.py set-target esp32
idf.py build
idf.py -p COMx flash monitor
```

### 3.4 方式 B：`bmelod_configure`

```cmake
include(${BMELOD_ROOT}/cmake/bmelod.cmake)

bmelod_configure(
    ROOT ${BMELOD_ROOT}
    PROFILE event
    BACKEND sdk_esp32_idf
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/bm_config_app.h
)

idf_component_register(SRCS "main.c" INCLUDE_DIRS ".")

bmelod_link(${COMPONENT_LIB})
```

`BACKEND` **必须**为 `sdk_esp32_idf`，**不要**用 `external`（后者需要应用 `bm_port.c`）。

---

## 4. 备选路径：独立 CMake（不出烧录固件）

```bash
export IDF_PATH=/path/to/esp-idf
cmake -B build -S . \
  -DBM_BUILD_TESTS=OFF \
  -DBM_BACKEND=sdk_esp32_idf \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-xtensa-esp32.cmake
```

独立 CMake 仅注入 IDF 头文件；**链接与烧录须在 `idf.py` 工程完成**。CI 烟雾：`tests/cmake/check_esp32_backend_configure.py`。

---

## 5. CMake / 配置对照

| 选项 / 文件 | ESP-IDF 工程中的建议值 |
|-------------|------------------------|
| `BM_BACKEND` | `sdk_esp32_idf` |
| `BM_CONFIG_FILE` | `main/bm_config_app.h` |
| 链接目标 | `bm_hal_esp32wroom32e` + `bm_framework` |
| `BM_BUILD_TESTS` | `OFF` |
| PROFILE | 见 [01-CMake选项与bm_config](../02-构建与工具链/01-CMake选项与bm_config.md) |

---

## 6. QEMU Xtensa 仿真（非真机 IDF）

`tests/qemu` 提供 QEMU Xtensa SMP 烟雾路径，用于 CI，**不是**灯哥板 IDF 固件替代。见 [tests/qemu/README.md](../../tests/qemu/README.md)。

---

## 7. 板级扩展

1. 更新 `bm_hal_instances_esp32wroom32e.h`（引脚宏）。
2. 在 `portable/vendor/esp32_idf/` 扩展 PWM/ADC 等 `bm_drv_*_api`。
3. 应用绑定方式同 [01-HAL契约与移植要点](01-HAL契约与移植要点.md)。

---

## 8. 常见问题

| 现象 | 处理 |
|------|------|
| `IDF_PATH is required` | 使用 `idf.py` 工程，或 export IDF 环境 |
| 重复 `bm_drv_timer_api` | 勿同时用 `external` 与 `sdk_esp32_idf`；删除 `bm_port.c` |
| undefined `bm_drv_*` | 补链 `bm_hal_esp32wroom32e` |
| `bm_config` 宏缺失 | `bm_config_app.h` 勿定义 `BM_CONFIG_H` |
| UART 无输出 | 检查 `menuconfig` 控制台 UART 与波特率 |

---

## 9. 相关文档

| 主题 | 文档 |
|------|------|
| CMake 选项 | [01-CMake选项与bm_config](../02-构建与工具链/01-CMake选项与bm_config.md) |
| Port 契约 | [03-Port移植层bm_port](03-Port移植层bm_port.md) |
| 组件速览 | [portable/vendor/esp32_idf/README.md](../../portable/vendor/esp32_idf/README.md) |
| `main` 骨架 | [02-main骨架与数据流](../01-应用开发/02-main骨架与数据流.md) |
| Demo 配置约定 | [01-Demo示例与运行路径](../01-应用开发/01-Demo示例与运行路径.md) §5 |
