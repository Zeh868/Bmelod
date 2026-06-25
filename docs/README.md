# Bmelod Baremetal 使用指南

本文档集仅覆盖**框架使用**：如何跑起来、如何写应用、如何构建、如何移植、如何排障、API 速查。不含架构设计、路线图或实施计划。

---

## 目录结构

```text
docs/
├── README.md
├── 00-入门与快速开始/          环境与首次运行
├── 01-应用开发/                Demo、main 骨架、事件、混合域
├── 02-构建与工具链/            CMake、预编译静态库
├── 03-移植与IDE集成/           HAL、挂库、Port、各 IDE、ESP-IDF
├── 04-测试与排障/              单元测试、迁移、运行时约束
└── 05-API参考/                 按头文件专题
```

---

## 章内索引

### 00 入门与快速开始

| 文档 | 说明 |
|------|------|
| [00-环境与首次运行](00-入门与快速开始/00-环境与首次运行.md) | 10 分钟内构建并运行示例或单元测试 |

### 01 应用开发

| 文档 | 说明 |
|------|------|
| [01-Demo示例与运行路径](01-应用开发/01-Demo示例与运行路径.md) | 示例矩阵、构建脚本、硬件移植 |
| [02-main骨架与数据流](01-应用开发/02-main骨架与数据流.md) | Nano / Control `main` 骨架 |
| [03-bmelod头文件与include](01-应用开发/03-bmelod头文件与include.md) | `bmelod.h` 与 `include/` 布局 |
| [04-事件模块与通道](01-应用开发/04-事件模块与通道.md) | `bm_event`、模块、通道、看门狗 |
| [05-混合域接线](01-应用开发/05-混合域接线.md) | HRT、`bm_exec`、snapshot、sync、stream |

### 02 构建与工具链

| 文档 | 说明 |
|------|------|
| [01-CMake选项与bm_config](02-构建与工具链/01-CMake选项与bm_config.md) | CMake 选项与 `bm_config.h` |
| [02-预编译静态库](02-构建与工具链/02-预编译静态库.md) | 预编译 `bm_core` 等静态库 |

### 03 移植与 IDE 集成

| 文档 | 说明 |
|------|------|
| [01-HAL契约与移植要点](03-移植与IDE集成/01-HAL契约与移植要点.md) | HAL 契约与移植检查项 |
| [02-挂库到现有工程](03-移植与IDE集成/02-挂库到现有工程.md) | Cube / SDK / Keil / IAR 挂库 |
| [03-Port移植层bm_port](03-移植与IDE集成/03-Port移植层bm_port.md) | `bm_port.c` 模板与后端 |
| [04-STM32-CubeMX集成](03-移植与IDE集成/04-STM32-CubeMX集成.md) | STM32 CubeMX |
| [05-NXP-MCUXpresso集成](03-移植与IDE集成/05-NXP-MCUXpresso集成.md) | NXP MCUXpresso |
| [06-Keil-MDK集成](03-移植与IDE集成/06-Keil-MDK集成.md) | Keil MDK |
| [07-IAR-EWARM集成](03-移植与IDE集成/07-IAR-EWARM集成.md) | IAR EWARM |
| [08-ESP-IDF与灯哥平衡车集成](03-移植与IDE集成/08-ESP-IDF与灯哥平衡车集成.md) | ESP-IDF + 灯哥板 |

### 04 测试与排障

| 文档 | 说明 |
|------|------|
| [01-单元测试与排障](04-测试与排障/01-单元测试与排障.md) | 单元测试与调试 |
| [02-版本迁移与演进](04-测试与排障/02-版本迁移与演进.md) | Ultra → Core、版本迁移 |
| [03-运行时约束与排障](04-测试与排障/03-运行时约束与排障.md) | fail-stop、禁止项、排障表 |

### 05 API 参考

[05-API参考/](05-API参考/)（`bm_hrt`、`bm_exec`、`bm_bus`、`bm_algorithm` 等）

---

## 推荐阅读路径

### 路径 A：首次运行（约 30 分钟）

1. [00-环境与首次运行](00-入门与快速开始/00-环境与首次运行.md) — 构建 `core_sensor` 或跑单元测试
2. [01-Demo示例与运行路径](01-应用开发/01-Demo示例与运行路径.md) — 浏览示例矩阵
3. [02-main骨架与数据流](01-应用开发/02-main骨架与数据流.md) — 对照 `main` 结构

### 路径 B：编写 Control 应用

1. [03-bmelod头文件与include](01-应用开发/03-bmelod头文件与include.md)
2. [04-事件模块与通道](01-应用开发/04-事件模块与通道.md) — SRT 协作
3. [05-混合域接线](01-应用开发/05-混合域接线.md) — HRT / `bm_exec` / snapshot
4. [02-构建与工具链/01-CMake选项与bm_config](02-构建与工具链/01-CMake选项与bm_config.md)
5. [05-API参考/](05-API参考/) — 按需查阅

### 路径 C：移植到 MCU

1. [03-移植与IDE集成/02-挂库到现有工程](03-移植与IDE集成/02-挂库到现有工程.md)
2. [03-移植与IDE集成/03-Port移植层bm_port](03-移植与IDE集成/03-Port移植层bm_port.md)
3. [03-移植与IDE集成/01-HAL契约与移植要点](03-移植与IDE集成/01-HAL契约与移植要点.md)
4. 按 IDE 选读 CubeMX / MCUX / Keil / IAR 集成文档

### 路径 D：灯哥平衡车 / ESP32

1. [03-移植与IDE集成/08-ESP-IDF与灯哥平衡车集成](03-移植与IDE集成/08-ESP-IDF与灯哥平衡车集成.md)
2. [portable/vendor/esp32_idf/README.md](../portable/vendor/esp32_idf/README.md)
3. [03-移植与IDE集成/01-HAL契约与移植要点](03-移植与IDE集成/01-HAL契约与移植要点.md)

排障与约束：[04-测试与排障/03-运行时约束与排障](04-测试与排障/03-运行时约束与排障.md)。

---

仓库入口：[README.zh-CN.md](../README.zh-CN.md)
