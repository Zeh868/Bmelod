# core_sensor

## 概述

**Nano 域**入门示例：演示 `bm_module` 生命周期、`bm_event` 发布/订阅，以及 `bm_mempool` 内存池在传感器数据传递中的用法。

## 层级与成熟度

| 项 | 值 |
|---|---|
| 执行域 | Nano |
| 成熟度 | D0（机制演示） |

## 学习重点

- `BM_MODULE_DEFINE` 与 `module_table.c` 模块注册
- 事件类型注册、`bm_event_publish_copy` 与订阅回调
- 多模块协作：采集 → 事件 → 日志/显示

## 关键机制与组件

| 模块 | 职责 |
|---|---|
| `mod_sensor` | 周期性采集温湿度并发布 `EVENT_TEMP` |
| `mod_events` | 事件路由与处理 |
| `mod_logger` | 日志输出 |
| `mod_display` | 显示/打印样本 |

## 目录结构

```text
core_sensor/
  main.c
  bm_config_app.h
  modules/
    module_table.c
    mod_sensor.c / mod_events.c / mod_logger.c / mod_display.c
  CMakeLists.txt
  Makefile          # 可选 Makefile 构建
```

## 构建与运行

本示例**仅支持 QEMU**。

```powershell
.\tools\demo\run_qemu.ps1 core_sensor
```

构建产物：`build/demo/<variant>/core_sensor/core_sensor.elf`

## 验收标准

- 串口输出 `EXAMPLE_CORE: PASS`
- 温湿度样本经事件链正确打印

## 相关文档

- [01-Demo示例与运行路径](../../docs/01-应用开发/01-Demo示例与运行路径.md)
- [02-main骨架与数据流](../../docs/01-应用开发/02-main骨架与数据流.md)
