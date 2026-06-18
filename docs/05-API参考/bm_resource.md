# bm_resource — 硬件资源声明与冲突检测

头文件：`include/bm_resource.h`  
实现：`Source/hybrid/bm_resource.c`

## 概述

控制实例通过 `bm_resource_claim_t` 声明对外设资源的访问模式。`bm_exec_start_all` 启动前调用冲突检测，防止多实例独占同一 PWM、ADC 规则组等。

## 类型

### `bm_resource_kind_t`

`PWM`、`ADC_GROUP`、`TIMER`、`DMA_CHANNEL`、`GPIO`、`COMP`、`IRQ`

### `bm_resource_access_t`

| 模式 | 含义 |
|------|------|
| `EXCLUSIVE` | 独占，同 key 不得有其他声明 |
| `OWNER` | 所有者；同 `share_group` 可有 `SHARED_READ` |
| `SHARED_READ` | 只读共享，需同组存在唯一 `OWNER` |
| `SHARED_COORDINATED` | 协调式共享（如多实例分时 ADC） |

### `bm_resource_claim_t`

| 字段 | 说明 |
|------|------|
| `kind` | 资源类别 |
| `key` | 平台相关句柄（如 `(uintptr_t)&BM_HAL_PWM_TIM1`） |
| `access` | 访问模式 |
| `share_group` | 共享组 ID，`EXCLUSIVE` 时忽略 |
| `name` | 调试名 |

## API

### `bm_resource_check_conflicts(claims, claim_counts, instance_count)`

`claims[i]` 指向第 i 个实例的声明数组，`claim_counts[i]` 为条数（单实例 ≤ `BM_CONFIG_MAX_RESOURCE_CLAIMS`，总展平条数有界）。  
返回：`BM_OK` 无冲突；`BM_ERR_INVALID`（NULL、枚举越界、条数超限）；`BM_ERR_BUSY` 等资源冲突码。

## 规则摘要

1. 同一 `kind + key` 最多一个 `EXCLUSIVE` 或 `OWNER`。
2. `SHARED_READ` 要求同 `share_group` 恰好一个 `OWNER`。
3. `EXCLUSIVE` 与任何其他模式在同 key 上互斥。
4. `SHARED_READ` 与 `SHARED_READ` 可共存；`SHARED_COORDINATED` 仅与同 `share_group` 的另一 `SHARED_COORDINATED` 兼容。
5. `kind` / `access` 须在枚举定义界内，否则 `BM_ERR_INVALID`。

## 示例

```c
static const bm_resource_claim_t g_claims[] = {
    { BM_RESOURCE_CLASS_PWM, (uintptr_t)&BM_HAL_PWM_TIM1,
      BM_RESOURCE_EXCLUSIVE, 0, "tim1_pwm" },
    { BM_RESOURCE_CLASS_ADC_GROUP, (uintptr_t)&BM_HAL_ADC1,
      BM_RESOURCE_OWNER, 1, "adc1_owner" },
};
```
