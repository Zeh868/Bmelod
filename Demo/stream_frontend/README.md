# stream_frontend

## 概述

**DSP 域**块流前端示例：在 `bm_stream` 之上使用 `bm_stream_frontend` 包装生产/提交与消费路径，Periodic 槽模拟 DMA 正弦 PCM，消费侧计算块级 RMS，监督层通过 event 启停并周期性打印漂移与 overrun 遥测。

## 层级与成熟度

| 项 | 值 |
|---|---|
| 执行域 | DSP（块流） |
| 成熟度 | E1 |

## 学习重点

- `bm_stream_frontend`：`on_block_submit` / `on_block_consume` 与漂移估计
- 双 Periodic Exec 槽（生产 + 消费），比 Block 槽更贴近前端封装
- 监督层 event 启停与 telemetry 轮询

## 关键机制与组件

- `bm_stream_frontend`、`bm_stream`、`bm_exec`
- `mod_supervisor`

## 目录结构

```text
stream_frontend/
  main.c
  app_stream_frontend.h
  bm_config_app.h
  modules/
  CMakeLists.txt
```

## 信号参数

- 采样率 32 kHz，块长 32 点
- 1 kHz 正弦输入

## 构建与运行

```powershell
.\tools\demo\run_native.ps1 stream_frontend
.\tools\demo\run_qemu.ps1 stream_frontend
```

## 验收标准

- 块 RMS ≈ 0.707
- 处理块数 ≥ 80
- 监督遥测读取次数 > 0

## 相关文档

- [01-Demo示例与运行路径](../../docs/01-应用开发/01-Demo示例与运行路径.md)
- 前置：`stream_block_rms`
