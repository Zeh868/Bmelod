# stream_bms_pipeline

## 概述

**DSP/BMS 域**块流管线示例：模拟 DMA 输入 Pack 充电电流块，经 `bm_pipeline` 串联 LPF 滤波与库仑 SOC 积分节点，演示信号处理链与 BMS 算法结合。

## 层级与成熟度

| 项 | 值 |
|---|---|
| 执行域 | DSP + BMS |
| 成熟度 | E1 |

## 学习重点

- `bm_pipeline` 线性节点链：LPF → 库仑积分
- `bm_algo_lpf1`、`bm_algo_coulomb`
- 块流恒定充电电流输入场景
- 与周期 Exec 版 `bms_estimation_loop` 的对比

## 关键机制与组件

- `bm_stream`、`bm_pipeline`
- `bm_algo_lpf1`、`bm_algo_coulomb`
- `mod_supervisor`

## 目录结构

```text
stream_bms_pipeline/
  main.c
  app_bms_pipeline.h
  bm_config_app.h
  modules/
  CMakeLists.txt
```

## 构建与运行

```powershell
.\tools\demo\run_native.ps1 stream_bms_pipeline
.\tools\demo\run_qemu.ps1 stream_bms_pipeline
```

## 验收标准

- 处理块数 ≥ 75
- SOC ≥ 0.515

## 相关文档

- [01-Demo示例与运行路径](../../docs/01-应用开发/01-Demo示例与运行路径.md)
- 周期版：`bms_estimation_loop`
