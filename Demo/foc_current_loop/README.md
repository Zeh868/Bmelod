# foc_current_loop

## 概述

**Control 域**有感 FOC **电流环**示例（E1）：固定电角度 θ=0，完成 Clarke/Park → dq 电流 PI → 逆 Park → SVPWM → RL 电气 plant 全链路，不含速度环与编码器。

## 层级与成熟度

| 项 | 值 |
|---|---|
| 执行域 | Control |
| 成熟度 | E1 |

## 学习重点

- `bm_algorithm` 电机数学核：Clarke、Park、SVPWM、PI
- 双分流电阻 ADC 电流采样仿真
- `bm_exec` 10 kHz 电流环周期槽
- 故障注入与 PWM 安全态

## 关键机制与组件

- `bm_algo_motor`、`bm_algo_control`（PI）
- `bm_hal_adc_sim`、`bm_hal_pwm_sim`
- 完整双环 FOC 见 `motor_foc_sensored`

## 目录结构

```text
foc_current_loop/
  main.c
  app_foc.h
  bm_config_app.h
  CMakeLists.txt
```

## 构建与运行

```powershell
.\tools\demo\run_native.ps1 foc_current_loop
.\tools\demo\run_qemu.ps1 foc_current_loop
```

## 验收标准

- iq 稳态跟踪误差 < 0.25 A
- 故障注入后 PWM 进入安全态

## 相关文档

- [01-Demo示例与运行路径](../../docs/01-应用开发/01-Demo示例与运行路径.md)
- 进阶：`motor_foc_sensored`
