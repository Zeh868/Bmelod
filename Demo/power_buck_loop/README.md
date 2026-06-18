# power_buck_loop

## 概述

**Control 域** Buck 双环电源控制示例：电压外环（10 ms）+ 电流内环（1 ms），使用 `bm_power_control` 领域组件驱动一阶 V/I plant 仿真。

## 层级与成熟度

| 项 | 值 |
|---|---|
| 执行域 | Control |
| 成熟度 | E1 |

## 学习重点

- `bm_power_control` 双环分时 Exec
- 电压斜坡与电流限幅
- 命令/遥测快照与故障锁存
- duty → 输出电压/电流一阶 plant

## 关键机制与组件

- `bm_power_control`（`power_control.c`）
- `bm_algo_pi`、`bm_algo_ramp`
- `mod_supervisor`

## 目录结构

```text
power_buck_loop/
  main.c
  bm_config_app.h
  modules/
  CMakeLists.txt
```

## 构建与运行

```powershell
.\tools\demo\run_native.ps1 power_buck_loop
.\tools\demo\run_qemu.ps1 power_buck_loop
```

## 验收标准

- 输出电压 ≥ 0.5 V（达到稳态）
- 故障注入后 `fault_latched` 置位

## 相关文档

- [01-Demo示例与运行路径](../../docs/01-应用开发/01-Demo示例与运行路径.md)
