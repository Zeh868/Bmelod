# 16 STM32 CubeMX 集成

> **本文职责**：在 STM32CubeMX / CubeIDE 工程中接入 Bmelod 的步骤要点。  
> **通用挂库模型** → [02-挂库到现有工程](02-挂库到现有工程.md)。

## 1. 步骤概览

| 步骤 | 操作 |
|------|------|
| ① 移植 | 复制 [`portable/template/bm_port.c`](../portable/template/bm_port.c) → `Core/Src/bm_port.c` |
| ② 配置 | 在应用 include 路径放置 `bm_config.h` |
| ③ 集成 | 添加 `Source/` 源文件或链接 `libbm_*.a`（见 [../02-构建与工具链/02-预编译静态库](../02-构建与工具链/02-预编译静态库.md)） |

Cube 生成的 `startup`、`HAL`、`main.c` **保持不变**；Bmelod 通过 Port 调用现有 `HAL_*`。

## 2. CMake 工程（CubeIDE / 外挂 CMake）

在厂商 `CMakeLists.txt` 末尾追加：

[`cmake/integration-snippet/CMakeLists.append.txt`](../cmake/integration-snippet/CMakeLists.append.txt)

```cmake
include(.../cmake/bmelod.cmake)
bmelod_configure(ROOT ... PROFILE event BACKEND external CONFIG Core/Inc/bm_config.h)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE Core/Src/bm_port.c)
bmelod_link(${CMAKE_PROJECT_NAME})
```

## 3. Keil / IAR

若使用 MDK 或 EWARM 而非 CMake，见 [06-Keil-MDK集成](06-Keil-MDK集成.md)、[07-IAR-EWARM集成](07-IAR-EWARM集成.md)。

## 4. 参考 Port

CMSIS 参考后端：`portable/vendor/stm32g4/`（`packs/sdk_stm32g4`，`-DBM_STM32_CUBE_PATH=...`）。量产推荐在 `bm_port.c` 内直接调用 Cube `HAL_*`。
