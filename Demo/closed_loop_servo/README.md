# closed_loop_servo

## 概述

**Control 域**闭环伺服示例：速度斜坡 → PI 速度环 → 一阶 plant 仿真，验证 `bm_algorithm` 与 `bm_exec` 协作，并演示故障注入后的 PWM 安全停机。

## 层级与成熟度

| 项 | 值 |
|---|---|
| 执行域 | Control |
| 成熟度 | E1（算法可演示） |

## 学习重点

- `bm_algo_ramp` 速度参考斜坡
- `bm_algo_pi` 速度 PI 调节
- 命令/遥测三缓冲 `bm_snapshot` 无锁交换
- `app_servo_inject_fault()` 故障锁存与安全态

## 关键机制与组件

- `bm_exec` 1 ms 周期槽
- `bm_hal_adc_sim`、`bm_hal_pwm_sim`
- `mod_supervisor`：监督与验收

## 目录结构

```text
closed_loop_servo/
  main.c
  app_servo.h
  bm_config_app.h
  modules/
  CMakeLists.txt
```

## 构建与运行

```powershell
.\tools\demo\run_native.ps1 closed_loop_servo
.\tools\demo\run_qemu.ps1 closed_loop_servo
```

## 验收标准

1. 速度跟踪达到 ≥ 1.5 rad/s
2. 故障注入后 PWM 安全停机且故障锁存

## 相关文档

- [01-Demo示例与运行路径](../../docs/01-应用开发/01-Demo示例与运行路径.md)
