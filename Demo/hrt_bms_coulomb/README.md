# hrt_bms_coulomb

## 概述

**Control 域** BMS 库仑计示例：Pack 级 ADC 硬件槽采样电流，经 `bm_snapshot` 传递给电芯级库仑积分 Exec，并演示比较器过压触发 PWM 安全停机。

## 层级与成熟度

| 项 | 值 |
|---|---|
| 执行域 | Control（HRT + Exec） |
| 成熟度 | D0（机制演示） |

## 学习重点

- 多级 Exec：Pack 采样槽 + Ticker 积分槽
- `bm_algo_coulomb` 库仑 SOC 积分
- `bm_hal_comp_sim` 比较器 trip 与安全输出
- 跨 HRT 快照传递 Pack 电流

## 关键机制与组件

- `bm_exec`、`bm_snapshot`、`bm_algo_coulomb`
- `bm_hal_adc_sim`、`bm_hal_pwm_sim`、`bm_hal_comp_sim`
- 共享逻辑：`bms_demo_shared`

## 目录结构

```text
hrt_bms_coulomb/
  main.c
  app_bms.h
  bm_config_app.h
  modules/
  CMakeLists.txt
```

## 构建与运行

```powershell
.\tools\demo\run_native.ps1 hrt_bms_coulomb
.\tools\demo\run_qemu.ps1 hrt_bms_coulomb
```

## 验收标准

- SOC 随充电电流上升
- 模拟比较器过压后 PWM 进入安全关断状态

## 相关文档

- [01-Demo示例与运行路径](../../docs/01-应用开发/01-Demo示例与运行路径.md)
- [bm_algorithm API](../docs/05-API参考/bm_algorithm.md)
