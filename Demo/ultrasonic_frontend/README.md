# ultrasonic_frontend

## 概述

**DSP 域**超声前端块流示例：合成 40 kHz 载波回波突发，经 ToF（飞行时间）检测与同步检波提取回波幅度，演示超声测距信号链。

## 层级与成熟度

| 项 | 值 |
|---|---|
| 执行域 | DSP（块流） |
| 成熟度 | E1 |

## 学习重点

- `bm_algo_detection`：ToF 检测、同步检波
- 块流超声突发信号合成
- 期望 ToF 索引与容差验收

## 关键机制与组件

- `bm_stream`
- `bm_algo_detection`（ToF、同步检波）
- `mod_supervisor`

## 目录结构

```text
ultrasonic_frontend/
  main.c
  bm_config_app.h
  modules/
  CMakeLists.txt
```

## 信号参数

- 载波 40 kHz
- 期望 ToF 索引 20

## 构建与运行

```powershell
.\tools\demo\run_native.ps1 ultrasonic_frontend
.\tools\demo\run_qemu.ps1 ultrasonic_frontend
```

## 验收标准

- 处理块数 ≥ 60
- ToF 索引在期望 ±4 范围内
- 检波幅度 ≥ 0.08

## 相关文档

- [01-Demo示例与运行路径](../../docs/01-应用开发/01-Demo示例与运行路径.md)
