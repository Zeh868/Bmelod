# multi_axis_sync

## 概述

**Control 域**多轴同步示例：使用 `bm_sync` 在域触发时刻同时启动三路 PWM Exec，验证多执行实例的相位对齐启动。

## 层级与成熟度

| 项 | 值 |
|---|---|
| 执行域 | Control |
| 成熟度 | D0（机制演示） |

## 学习重点

- `bm_sync_configure` / `arm` / `trigger` / `safe_stop` 流程
- 多 `bm_exec` 实例绑定不同 `bm_hal_pwm_sim` 通道
- 启动 tick 对齐验收

## 关键机制与组件

- `bm_sync`
- 3 × `bm_exec` + `BM_HAL_PWM_SIM0/1/2`

## 目录结构

```text
multi_axis_sync/
  main.c
  bm_config_app.h
  CMakeLists.txt
```

## 构建与运行

```powershell
.\tools\demo\run_native.ps1 multi_axis_sync
.\tools\demo\run_qemu.ps1 multi_axis_sync
```

## 验收标准

- `bm_sync_trigger` 后三轴 `start_tick` 完全相同
- 通过后执行 `bm_sync_safe_stop`

## 相关文档

- [01-Demo示例与运行路径](../../docs/01-应用开发/01-Demo示例与运行路径.md)
- [03-跨域通道与源码对照](../../docs/01-应用开发/05-混合域接线.md)
