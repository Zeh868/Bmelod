# bm_ticker — 毫秒级周期事件发布器

头文件：`include/bm_ticker.h`  
实现：`Source/hybrid/bm_ticker.c`

## 概述

Ticker 在慢速域按固定毫秒周期向事件总线发布事件，供后台任务轮询处理。须在主循环中周期性调用 `bm_ticker_poll()`。

## 类型

| 类型 | 字段 |
|------|------|
| `bm_ticker_slot_t` | `period_ms`、`event_type`、`priority`、`name` |

## API

### `bm_ticker_init(slots, slot_count)`

注册 ticker slot 表。校验 `period_ms` 非零、`event_type` / `priority` 在配置界内，且 `bm_hal_timer_get_freq()` 非零。  
返回：`BM_OK`；`BM_ERR_INVALID`；`BM_ERR_OVERFLOW`（槽数超限）；`BM_ERR_ALREADY`（重复 init）；`BM_ERR_NOT_INIT`（定时器未配置）。

### `bm_ticker_poll()`

检查各 slot 是否到期，到期则 `bm_event_publish_copy`。发布失败（队列满 `BM_ERR_OVERFLOW`、事件类型未注册、多核转发失败等）统一按丢弃处理：饱和递增对应 slot 的 `dropped` 计数、推进该槽 `next_us`，然后 `break` 出内层循环处理下一槽——绝不提前 `return` 中断整轮 slot 遍历，避免同一槽反复复现同一错误、拖累后续槽全部被跳过。catchup 预算耗尽仍落后时按 resync 补记：把本次静默跳过的周期数一并计入该槽 `dropped`，口径与发布失败路径一致。tick 回绕不会产生额外发布。
返回：本次发布数；负值仅为 `BM_ERR_NOT_INIT`（未初始化）。

### `bm_ticker_get_dropped(slot_index)`

返回指定 slot 因队列满丢弃的事件数（临界区内读取），用于监控背压。索引无效返回 0。

### `bm_ticker_reset()`

清空内部计时与 dropped 计数。

### `bm_ticker_is_initialized()`

在临界区内查询初始化状态。`reset` 在清理槽表之前先发布未初始化
状态，因此监督逻辑不会观察到已失效槽表仍处于可用状态。

## 使用示例

```c
static const bm_ticker_slot_t g_ticker[] = {
    { 100u, BM_EVENT_TICK_100MS, BM_EVENT_PRIO_LOW, "slow" },
};

bm_ticker_init(g_ticker, BM_ARRAY_SIZE(g_ticker));

for (;;) {
    bm_ticker_poll();
    bm_event_process();
}
```

## 与 HRT 的边界

| 域 | 周期 | 上下文 | 输出 |
|----|------|--------|------|
| HRT | μs | ISR | 直接回调 |
| Ticker | ms | 主循环 | `bm_event_publish` |

不要在 HRT 回调中调用 ticker API。
