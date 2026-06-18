# stream_block_rms

## 概述

**DSP 域**块流基础示例：Periodic Exec 槽模拟 DMA 生产音频块，Block Exec 槽消费并计算块级 RMS，演示 `bm_stream` 零拷贝块交接。

## 层级与成熟度

| 项 | 值 |
|---|---|
| 执行域 | DSP（块流） |
| 成熟度 | E1 |

## 学习重点

- `bm_stream` 生产者/消费者模型
- `bm_exec` Periodic 槽 + Block 槽分工
- 块内 RMS 计算（本示例手写，未用 pipeline）
- 是 `daq_frontend` 的简化前置版

## 关键机制与组件

- `bm_stream`、`bm_exec`（Block 槽）
- `mod_supervisor`

## 目录结构

```text
stream_block_rms/
  main.c
  app_stream.h
  bm_config_app.h
  modules/
  CMakeLists.txt
```

## 信号参数

- 采样率 32 kHz，块长 32 点
- 输入 1 kHz 正弦（幅值 1.0）

## 构建与运行

```powershell
.\tools\demo\run_native.ps1 stream_block_rms
.\tools\demo\run_qemu.ps1 stream_block_rms
```

## 验收标准

- 块 RMS ≈ 0.707（1/√2）
- 处理块数 ≥ 80

## 相关文档

- [01-Demo示例与运行路径](../../docs/01-应用开发/01-Demo示例与运行路径.md)
- 进阶：`daq_frontend`
