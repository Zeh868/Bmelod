# stream_fft

## 概述

**DSP 域**块流 FFT 示例：DMA 流仿真产生固定长度音频块，经 RFFT 计算频谱并查找主峰，验证块级频域分析链路。

## 层级与成熟度

| 项 | 值 |
|---|---|
| 执行域 | DSP（块流） |
| 成熟度 | E1 |

## 学习重点

- `bm_hal_dma_stream_sim` 模拟 DMA 块生产
- `bm_algo_rfft` 实数 FFT 与频谱峰搜索
- Block Exec 槽消费与块对齐

## 关键机制与组件

- `bm_stream`、`bm_exec`、`bm_hal_dma_stream_sim`
- `bm_algo_rfft`
- `mod_supervisor`

## 目录结构

```text
stream_fft/
  main.c
  app_fft.h
  bm_config_app.h
  modules/
  CMakeLists.txt
```

## 信号参数

- 采样率 16 kHz，块长 64 点，块周期 4 ms
- 1 kHz 正弦 → 主峰应在 bin 4

## 构建与运行

```powershell
.\tools\demo\run_native.ps1 stream_fft
.\tools\demo\run_qemu.ps1 stream_fft
```

> QEMU 仿真周期较长（约 150k cycles），请耐心等待。

## 验收标准

- 处理块数 ≥ 50
- 频谱主峰 bin 与预期一致

## 相关文档

- [01-Demo示例与运行路径](../../docs/01-应用开发/01-Demo示例与运行路径.md)
