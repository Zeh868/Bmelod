# full_system

## 概述

**Lite 域**全系统集成示例：多模块协作、事件风暴优先级、看门狗喂狗与故障注入，展示接近真实产品的应用骨架。

## 层级与成熟度

| 项 | 值 |
|---|---|
| 执行域 | Lite |
| 成熟度 | D0（机制演示） |

## 学习重点

- `#include "bmelod.h"` 一站式框架入口
- `bm_wdg` 看门狗与主循环喂狗
- 多优先级事件风暴的顺序保证
- 传感器故障注入与 `mod_fault` 处理

## 关键机制与组件

| 模块 | 职责 |
|---|---|
| `mod_sensor` | 传感器数据与故障模拟 |
| `mod_comm` | 通信 |
| `mod_display` | 显示 |
| `mod_fault` | 故障处理 |
| `mod_events` | 事件调度 |

## 目录结构

```text
full_system/
  main.c
  app_events.c
  bm_config_app.h
  modules/
  CMakeLists.txt
  Makefile
```

## 构建与运行

本示例**仅支持 QEMU**。

```powershell
.\tools\demo\run_qemu.ps1 full_system
```

## 验收标准

- 第 3 周期发布 5 个不同优先级风暴事件且顺序正确
- 第 5 周期注入 `EVENT_SENSOR_FAULT` 后系统仍稳定
- 串口输出 `EXAMPLE_FULL: PASS`

## 相关文档

- [01-Demo示例与运行路径](../../docs/01-应用开发/01-Demo示例与运行路径.md)
- [02-main骨架与数据流](../../docs/01-应用开发/02-main骨架与数据流.md)
