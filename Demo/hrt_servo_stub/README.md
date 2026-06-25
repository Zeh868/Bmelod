# hrt_servo_stub

## 概述

**Control 域** HRT 伺服桩示例：ADC 硬件槽驱动电流环、1 ms 周期槽驱动速度环，配合 `bm_ticker` 发布位置事件，演示混合执行域的基本接线。

## 层级与成熟度

| 项 | 值 |
|---|---|
| 执行域 | Control（HRT + Exec） |
| 成熟度 | D0（机制演示） |

## 学习重点

- `bm_exec` 双槽：`BM_EXEC_SLOT_HARDWARE`（ADC 触发）+ Periodic（速度环）
- `bm_hal_adc_sim` / `bm_hal_pwm_sim` 仿真外设
- `bm_ticker` 低频事件发布

## 关键机制与组件

- `bm_exec`、`bm_hrt`、`bm_ticker`
- `mod_events`：处理 `EVENT_POSITION`

## 目录结构

```text
hrt_servo_stub/
  main.c
  bm_config_app.h
  modules/
  CMakeLists.txt
```

## 构建与运行

```powershell
.\tools\demo\run_native.ps1 hrt_servo_stub    # PC 仿真，退出码验收
.\tools\demo\run_qemu.ps1 hrt_servo_stub      # QEMU 长运行
```

## 验收标准

- `current_hits`、`speed_hits`、`position_events` 均 > 0
- native_sim 下进程以退出码 0 结束

## 相关文档

- [01-Demo示例与运行路径](../../docs/01-应用开发/01-Demo示例与运行路径.md)
- [bm_exec](../../docs/05-API参考/)（混合域 API）
