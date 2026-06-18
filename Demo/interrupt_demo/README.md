# interrupt_demo

## 概述

演示**板级中断 → 事件入队**的典型路径：SysTick 与 TIMER1 ISR 在中断上下文中发布事件，主循环通过 `bm_event` 异步处理，避免在 ISR 中做重逻辑。

## 层级与成熟度

| 项 | 值 |
|---|---|
| 执行域 | Nano |
| 成熟度 | D0（机制演示） |

## 学习重点

- `bm_event_publish_copy_from_isr` 中断安全发布
- ISR 与主循环职责分离（`interrupt_timer.c` 独立于 `main.c`）
- tick / 采样 / 统计三类事件的协作

## 关键机制与组件

| 模块 | 职责 |
|---|---|
| `interrupt_timer.c` | 板级定时器 ISR，发布 `EVENT_TICK` / `EVENT_SAMPLE` |
| `mod_hw` | 硬件初始化 |
| `mod_tick` / `mod_sample` / `mod_stats` | 事件消费与统计 |

## 目录结构

```text
interrupt_demo/
  main.c
  interrupt_timer.c / interrupt_timer.h   # 板级 ISR（移植时替换此文件）
  bm_config_app.h
  modules/
  CMakeLists.txt
  Makefile
```

## 构建与运行

本示例**仅支持 QEMU**。

```powershell
.\tools\demo\run_qemu.ps1 interrupt_demo
```

## 验收标准

- tick 计数 ≥ 20 且 sample ≥ 2 后输出 PASS
- 统计模块汇总 ISR 与主循环事件计数

## 移植提示

将 `interrupt_timer.c` 替换为目标 MCU 的定时器中断实现，保留事件发布接口即可。详见 [Demo/PORTING.md](../PORTING.md)。

## 相关文档

- [01-Demo示例与运行路径](../../docs/01-应用开发/01-Demo示例与运行路径.md)
