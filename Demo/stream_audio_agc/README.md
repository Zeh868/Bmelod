# stream_audio_agc

## 概述

**DSP 域**音频块流示例：低幅正弦经 `bm_pipeline` 串联 AGC 与限幅器节点，将输出拉升至目标电平，演示 `bm_algo_audio` 在块流管线中的用法。

## 层级与成熟度

| 项 | 值 |
|---|---|
| 执行域 | DSP（块流） |
| 成熟度 | E1 |

## 学习重点

- `bm_pipeline` 多节点：AGC + Limiter
- `bm_algo_agc` 自动增益控制
- 事件驱动块生产：`EVENT_AUDIO_ENABLE`

## 关键机制与组件

- `bm_stream`、`bm_pipeline`
- `bm_algo_agc`
- `mod_supervisor`

## 目录结构

```text
stream_audio_agc/
  main.c
  app_audio.h
  bm_config_app.h
  modules/
  CMakeLists.txt
```

## 信号参数

- 采样率 16 kHz，块长 64 点
- 输入幅值 0.1，AGC 目标 0.5

## 构建与运行

```powershell
.\tools\demo\run_native.ps1 stream_audio_agc
.\tools\demo\run_qemu.ps1 stream_audio_agc
```

需发布 `EVENT_AUDIO_ENABLE` 启动块生产（监督模块负责）。

## 验收标准

- 处理块数 ≥ 50
- 输出峰值 ≥ 0.35

## 相关文档

- [01-Demo示例与运行路径](../../docs/01-应用开发/01-Demo示例与运行路径.md)
