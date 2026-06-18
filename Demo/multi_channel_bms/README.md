# multi_channel_bms

## 概述

**Control 域**多通道 BMS 示例：Pack 级硬件槽遍历 16 路电芯 ADC，经 `bm_algo_lpf1` 滤波后检测过压，每路电芯独占 GPIO 资源声明。

## 层级与成熟度

| 项 | 值 |
|---|---|
| 执行域 | Control |
| 成熟度 | D0（机制演示）→ **E1**（LPF + Pack 指标占位） |

## 学习重点

- 单 Pack 槽多通道 ADC 轮询
- 动态构建 16 个 cell 级 `bm_exec`
- `bm_resource_claim` 每电芯 GPIO 独占
- `bm_snapshot` 跨槽传递电芯电压

## 关键机制与组件

- `bm_exec`、`bm_snapshot`、`bm_resource_claim`
- `bm_algo_lpf1`：5 Hz 低通滤波
- Pack 级 avg/min/max 电压与线性 SOC 占位（E1 诚实标注，非真实 OCV）
- `mod_events`：处理 `EVENT_CELL_CHECK`

## 目录结构

```text
multi_channel_bms/
  main.c
  app_bms.h
  bm_config_app.h
  modules/
  CMakeLists.txt
```

## 构建与运行

```powershell
.\tools\demo\run_native.ps1 multi_channel_bms
.\tools\demo\run_qemu.ps1 multi_channel_bms
```

## 验收标准

- 16 路电芯均有有效电压读数
- 滤波后电压 > 4200 mV 时记录过压标志
- 日志输出 Pack avg/min/max 电压与 `soc_est` 占位（%）

## 相关文档

- [components/multi_channel_bms/MATURITY.md](../../components/multi_channel_bms/MATURITY.md)

- [01-Demo示例与运行路径](../../docs/01-应用开发/01-Demo示例与运行路径.md)
