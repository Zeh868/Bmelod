# bms_estimation_loop

## 概述

**Control 域** BMS SOC 估算示例：演示 `bm_bms_estimation` 领域组件，包含库仑积分、静置 OCV 融合与温度容量补偿，配合内置仿真 plant 完成充放电场景。

## 层级与成熟度

| 项 | 值 |
|---|---|
| 执行域 | Control |
| 成熟度 | E1 |

## 学习重点

- `bm_bms_estimation` 组件配置与 `bm_exec` 周期调度
- 库仑计 + OCV 查表融合策略
- 静置判定与温度补偿
- 遥测发布与监督模块验收

## 关键机制与组件

- `bm_bms_estimation`（`bms_estimation.c`）
- `bm_algo_coulomb`、OCV 查表、SOC 融合
- `bm_snapshot`、`bm_ticker`
- `mod_supervisor`

## 目录结构

```text
bms_estimation_loop/
  main.c
  app_bms_est.h
  bm_config_app.h
  modules/
  CMakeLists.txt
```

## 构建与运行

```powershell
.\tools\demo\run_native.ps1 bms_estimation_loop
.\tools\demo\run_qemu.ps1 bms_estimation_loop
```

## 仿真场景

- 前段：20 A 充电电流
- 后段：静置，触发 OCV 校正

## 验收标准

- `soc_fused ≥ 0.52`
- 估算步数 ≥ 50
- 遥测读取次数 > 0

## 相关文档

- [01-Demo示例与运行路径](../../docs/01-应用开发/01-Demo示例与运行路径.md)
- 块流版：`stream_bms_pipeline`
