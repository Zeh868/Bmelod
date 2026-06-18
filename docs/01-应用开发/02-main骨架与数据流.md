# 07 main骨架与数据流

> **本文职责**：`main` 初始化顺序、示例数据流与常见排障。  
> **不负责**：重复 02–04 的原理说明。

从空白工程到跑起来：**`main` 写什么、数据怎么流**。机制接线见 [03](../01-应用开发/05-混合域接线.md)、[04](../01-应用开发/05-混合域接线.md)；示例列表见 [01-Demo](01-Demo示例与运行路径.md)。

## 1. 三层结构

```text
应用：bm_config.h、#include "bmelod.h"、模块表/控制实例表、main
框架：Source/core、Source/hybrid（CMake 链接 bm_core、bm_hrt 等）
HAL：portable/ 或板级 Port（须设 BM_BACKEND 并链 bm_hal_* 后端，勿只用弱符号桩）
```

---

## 2. 按层级选型

| 层级 | 组件 | 示例 |
|------|------|------|
| Nano | `bm_module` + `bm_event` | `core_sensor` |
| Nano+ | + `publish_copy_from_isr` | `interrupt_demo` |
| Lite | + `channel` / `shell` / `wdg` | `full_system` |
| Control | + `exec` / `hrt` / `snapshot` / `sync` | `hrt_servo_stub` |
| DSP | + `stream` / Block 槽 / `pipeline` | `stream_fft` |

CMake 选项：[../02-构建与工具链/01-CMake选项与bm_config](../02-构建与工具链/01-CMake选项与bm_config.md)。

---

## 3. Nano 骨架：`core_sensor`

### 目录与模块声明

```text
core_sensor/
  main.c                 # 事件、主循环、业务数据流
  modules/
    mod_display.c        # BM_MODULE_DEFINE(display, ...)
    mod_logger.c
    mod_sensor.c
    module_table.c       # BM_MODULE_TABLE(...)
```

生命周期回调写在各 `mod_*.c`；`main.c` 只调 `bm_module_init_all()` / `start_all()`。宏说明见 [04 §4](../01-应用开发/05-混合域接线.md#4-模块生命周期bm_module)。

### 初始化顺序

```c
bm_hal_uart_init(NULL);
bm_event_reset();
bm_event_register_type(...);
bm_event_subscribe(...);
bm_module_init_all();        /* 内部自动 bm_event_freeze_subscriptions() */
bm_module_start_all();
for (;;) {
    /* 采样 + publish_copy → 主循环固定顺序 */
    bm_event_process(8);
}
```

> `bm_module_init_all()` 成功后自动冻结事件订阅表与 relay 注册表。
> 冻结后 `subscribe`/`unsubscribe`/`register_type` 返回 `BM_ERR_BUSY`。

### 一周期数据流

```text
alloc → 填数据 → publish_copy → free 池对象
              →（异步入队）
bm_event_process → display_on_temp / logger_on_temp
```

事件接线详解：[04 §1](04-事件模块与通道.md#1-事件系统bm_event)。

---

## 4. Lite 骨架：`full_system`

在 Nano 主循环上增加 `bm_wdg_feed()`：单 main、事件协作、无分模块线程时，主循环每圈直接喂硬件狗（不注册软件模块）。见 [04 §6](../01-应用开发/05-混合域接线.md#6-软件看门狗bm_wdg)。

---

## 5. Control 骨架（混合域）

### 5.1 控制实例

每个伺服轴 / Pack 采样器一个 `bm_exec_t`，槽位类型：

| kind | 提供 |
|------|------|
| `BM_EXEC_SLOT_HARDWARE` | `run` + `bind` |
| `BM_EXEC_SLOT_PERIODIC` | `run` + `period_us` |
| `BM_EXEC_SLOT_BLOCK` | `run` + `bm_stream`（主循环 drain） |
| `BM_EXEC_SLOT_FRAME` | `run` + 帧级 stream（同上） |

Hardware / Scheduled 接线：[03 §3](../01-应用开发/05-混合域接线.md#3-hardware-hrt-用法)、[03 §4](../01-应用开发/05-混合域接线.md#4-scheduled-hrt-用法bm_hrt)。

### 5.2 初始化顺序

```c
bm_hal_timer_init(tick_hz);       /* 与 HRT/ticker 共用计数器 */
bm_exec_init_all(instances, n);   /* 组 HRT 表、bind Hardware、实例 init；会话 → INITED */
bm_sync_configure/arm(&domain);   /* 可选：可在 start_all 前完成 */
bm_exec_start_all(instances, n);  /* 全部 ops->start 后再 bm_hrt_start；会话 → STARTED */
bm_sync_trigger(&domain);         /* 可选：one-shot，重复须 bm_sync_arm */
bm_ticker_init(...);              /* 可选：subscribe 之后、定时器已 init */

for (;;) {
    bm_exec_drain_streams(4);  /* Block/Frame 槽；预算按应用调整 */
    bm_ticker_poll();
    bm_event_process();
    bm_wdg_feed();
}
```

| 常见错误 | 后果 |
|----------|------|
| 未调用 `bm_exec_start_all`（且无手动 `bm_hrt_start`） | Periodic / exec 绑定的 Hardware `run` 不执行 |
| 在 `start_all` 前手动 `bm_hrt_start` 且经 `exec` | 不推荐；会话门未开时 `step` 仍被忽略 |
| `sync_trigger` 后再次 trigger 未 re-arm | 返回 `BM_ERR_NOT_INIT` |
| `ticker_init` 在 `bm_hal_timer_init` 之前 | 返回 `BM_ERR_NOT_INIT` |
| 以为 `ticker_poll` 会调订阅者 | 必须再 `bm_event_process` |
| 仿真 Hardware 环无输出 | 须 `bm_hal_adc_sim_fire_complete` 或真机 IRQ |

### 5.3 运行时

```text
ISR:  Hardware 环（ADC 等） | Scheduled 环（bm_hrt tick）
主循环:
    stream drain → ticker → event → log → wdg
```

> **确定性流式**：Block 槽统一走 `bm_exec_drain_streams`，`on_ready` 同步回调已废弃；hard RT profile 下同样禁止。

---

## 6. 示例数据流（速查）

| 示例 | 数据流 |
|------|--------|
| `hrt_servo_stub` | ADC IRQ→`current_step`；TIM→`speed_step`；ticker→`EVENT_POSITION`→`process`；仿真用 `fire_complete` 模拟 ADC |
| `hrt_bms_coulomb` | Pack Hardware `SNAPSHOT_PUBLISH` 电流 → 电芯 Scheduled `READ` 积分 |
| `multi_channel_bms` | Pack 写 16 路 snapshot → ticker 事件 → SRT `READ` 过压判断 |
| `multi_axis_sync` | `sync_trigger` 后各轴 Scheduled `axis_step` 同 tick 记录 `start_tick` |
| `stream_fft` | DMA 块 → `bm_stream` → Block 槽 RFFT |
| `interrupt_demo` | `TIMER1_IRQHandler`→回调→`publish_from_isr`→`process` |

---

## 7. Nano → Control 升级清单

1. `bm_config.h` 增加 `BM_CONFIG_HRT_*`、`BM_CONFIG_MAX_EXEC_*`
2. CMake：`BM_ENABLE_HRT`、`BM_ENABLE_EXEC`（多轴 + `BM_ENABLE_SYNC`）
3. 环算法迁入 `bm_exec_slot_t.run`
4. HRT→SRT 数据改 `bm_snapshot`；通知保留事件
5. `bm_exec_init_all` 替代手写 `bm_hrt_init`（多实例）

---

## 8. 排障

| 现象 | 查 |
|------|-----|
| Periodic 不跑 | `bm_exec_start_all`（或 `bm_hrt_start`）、`period_us` 整除 `BM_CONFIG_HRT_TICK_US` |
| Hardware 不跑 | `bind` 返回值、HAL 是否强符号、仿真是否 `fire_complete` |
| 订阅不触发 | `register`/`subscribe`、`bm_event_process` |
| 多轴不同步 | sync 顺序、是否链 `bm_sync_hal_native` |
| 绑了没反应 | 是否只链 `bm_hal` 弱符号桩 → [01-HAL](../03-移植与IDE集成/01-HAL契约与移植要点.md) |

测试：[../04-测试与排障/01-单元测试与排障](../04-测试与排障/01-单元测试与排障.md)。
