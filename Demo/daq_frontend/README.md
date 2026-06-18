# daq_frontend

## 概述

**DSP 域**数据采集前端示例：在块流基础上集成 `bm_daq_frontend` 组件，实现块级 RMS、波峰因数、触发采集与预触发环形缓冲，比 `stream_block_rms` 功能更完整。

## 层级与成熟度

| 项 | 值 |
|---|---|
| 执行域 | DSP（块流） |
| 成熟度 | E1 |

## 学习重点

- `bm_daq_frontend`：武装、触发、后触发采集
- `bm_algo_rms` 滑动窗口与波峰因数
- `bm_stream` + 双 Exec 槽（Periodic 生产 + Block 消费）
- 预触发缓冲导出 API：`bm_daq_frontend_copy_pre_trigger`

## 关键机制与组件

- `bm_daq_frontend`、`bm_stream`、`bm_exec`
- `bm_algo_statistics`、`bm_algo_rms`、`bm_algo_power`
- `mod_supervisor`

## 目录结构

```text
daq_frontend/
  main.c
  app_daq.h
  bm_config_app.h
  modules/
  CMakeLists.txt
```

## 信号参数

- 采样率 32 kHz，块长 32 点
- 1 kHz 正弦输入

## 构建与运行

```powershell
.\tools\demo\run_native.ps1 daq_frontend
.\tools\demo\run_qemu.ps1 daq_frontend
```

QEMU 下若生产未自动启动，监督模块可补发 `EVENT_DAQ_ENABLE`。

## 验收标准

- RMS ≈ 0.707，波峰因数 ≈ 1.414
- 处理块数 ≥ 80

## 相关文档

- [01-Demo示例与运行路径](../../docs/01-应用开发/01-Demo示例与运行路径.md)
- 前置：`stream_block_rms`
