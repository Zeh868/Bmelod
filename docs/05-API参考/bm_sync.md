# bm_sync — 多实例硬件同步域

头文件：`include/bm_sync.h`  
实现：`Source/hybrid/bm_sync.c` + 平台 `bm_sync_hal_*.c`

## 概述

通过主定时器与 per-member 相位偏移，协调多个控制实例的 PWM 更新与采样时序。框架 API 校验参数并委托 HAL 完成 ITR/触发路由。

## 类型

### `bm_sync_domain_t`

| 字段 | 说明 |
|------|------|
| `name` | 同步域名称 |
| `master_timer` | 主 `bm_hal_timer_t` 指针 |
| `members` | `bm_exec_t` 指针数组 |
| `phase_ticks` | 各成员相对主定时器的相位（tick） |
| `member_count` | 成员数量 |

## API

### `bm_sync_configure(domain)`

配置主从关系与相位表，委托 HAL 设置硬件触发链。
重复成员会被拒绝；HAL 失败时立即执行 safe-stop 并清除活动域。
返回：`BM_OK`；`BM_ERR_INVALID`；HAL 错误码。

### `bm_sync_arm(domain)`

武装同步域，准备一次性或周期性触发。
HAL 失败时立即执行 safe-stop 并清除活动域。

### `bm_sync_trigger(domain)`

触发同步域，按 `phase_ticks` 释放各成员硬件相位（one-shot：成功后清除 armed，重复触发须先 `arm`）。  
QEMU 参考 HAL 在 `trigger` 时同步触发成员执行（仿真用）。
HAL 失败时立即执行 safe-stop，后续必须重新 configure。

### `bm_sync_safe_stop(domain)`

安全停止并复位 HAL 状态。

### `bm_sync_get_state()`

在临界区内读取框架层状态。稳定状态为 `IDLE`、`CONFIGURED` 或
`ARMED`；HAL 配置、武装、触发或安全停止尚未完成时返回
`TRANSITION`，监督逻辑不得把它当作可运行状态。

## 框架层校验

- `member_count` ≤ `BM_CONFIG_MAX_SYNC_MEMBERS`
- 各 `phase_ticks[i]` ≤ `BM_CONFIG_SYNC_MAX_PHASE_TICKS`
- `members[i]` 非 NULL

## 生命周期

```text
configure → arm → trigger → （须 re-arm 才能再次 trigger）→ safe_stop
```

`configure` / `arm` 可在 `bm_exec_start_all` 之前或之后完成；`trigger` 通常在 `start_all` 之后（Periodic 槽依赖 HRT 已启动）。示例见 `Demo/multi_axis_sync`。

## 平台实现

| 平台 | 文件 |
|------|------|
| native_sim | `portable/sim/native/bm_sync_hal_native.c` |
| QEMU M0 | `portable/sim/qemu_cm0/bm_sync_hal_qemu.c` |
| STM32G4 | `portable/vendor/stm32g4/bm_sync_hal_stm32g4.c` |

真实 MCU 上 `trigger` 通过定时器比较/ITR 硬件路由；仿真环境可退化为软件逐步调用。
