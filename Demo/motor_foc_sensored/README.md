# motor_foc_sensored

## 概述

**Control 域**完整有感 FOC 示例：10 kHz 电流环 + 1 kHz 编码器速度环，集成 `bm_motor_foc_sensored` 领域组件与含机械惯量的 RL/plant 仿真。

## 层级与成熟度

| 项 | 值 |
|---|---|
| 执行域 | Control |
| 成熟度 | E1 |

## 学习重点

- `bm_component_motor` 双环 FOC 组件用法
- `bm_hal_encoder_sim` 编码器反馈与电角度换算
- 双周期 `bm_exec`：电流槽 + 速度槽
- 速度跟踪与故障安全停机验收

## 关键机制与组件

- `bm_motor_foc_sensored`（`motor_foc_sensored.c`）
- `bm_hal_adc_sim`、`bm_hal_pwm_sim`、`bm_hal_encoder_sim`
- `mod_supervisor`

## 目录结构

```text
motor_foc_sensored/
  main.c
  app_motor.h
  bm_config_app.h
  board/                    # 板级包络（STM32G4 参考）
  modules/
  CMakeLists.txt
```

## 构建与运行

```powershell
.\tools\demo\run_native.ps1 motor_foc_sensored
.\tools\demo\run_qemu.ps1 motor_foc_sensored
```

## 验收标准

- 速度跟踪达到设定值
- 故障注入后 PWM 安全停机

## 相关文档

- [01-Demo示例与运行路径](../../docs/01-应用开发/01-Demo示例与运行路径.md)
- 前置：`foc_current_loop`
