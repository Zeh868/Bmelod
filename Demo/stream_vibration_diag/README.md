# stream_vibration_diag

## 概述

**DSP 域**振动诊断块流示例：合成加速度正弦块，经 `bm_pipeline` 同时计算块 RMS 与 Goertzel 单频幅度，用于轴承/旋转机械振动频谱监测演示。

## 层级与成熟度

| 项 | 值 |
|---|---|
| 执行域 | DSP（块流） |
| 成熟度 | E1 |

## 学习重点

- `bm_algo_spectral`（Goertzel）单频检测
- `bm_algo_statistics` 块 RMS
- `bm_spectral_diagnostics` 组件相关算法在管线中的集成
- 阶次跟踪与轴转速输入

## 关键机制与组件

- `bm_stream`、`bm_pipeline`
- `bm_algo_statistics`、`bm_algo_spectral`
- `mod_supervisor`

## 目录结构

```text
stream_vibration_diag/
  main.c
  app_vibration.h
  bm_config_app.h
  modules/
  CMakeLists.txt
```

## 构建与运行

```powershell
.\tools\demo\run_native.ps1 stream_vibration_diag
.\tools\demo\run_qemu.ps1 stream_vibration_diag
```

## 验收标准

- 处理块数 ≥ 40
- Goertzel 目标频率幅度达到预期

## 相关文档

- [01-Demo示例与运行路径](../../docs/01-应用开发/01-Demo示例与运行路径.md)
