# bm_wcet_mon — 运行时 deadline / WCET 监控（SAFE-2）

头文件：`include/bm/hybrid/bm_wcet_mon.h`
实现：`Source/hybrid/bm_wcet_mon.c`

## 概述

`bm_wcet_mon` 是 SAFE-2（运行时 deadline / WCET 监控，路线图 L3）的落地模块：
给框架补上"跑没跑成、跑了多久"这一层运行时事实基础——本模块只做**检测与
上报**，不做任何降级动作（降级是 SAFE-1 的职责边界，一票否决混入）。核心
抽象是**监控段（span）**：app/宿主自有存储的静态实例，`begin`/`end` 包住一段
代码测出实测耗时，与登记的 `budget_us` 比对判超；上报走**双层**——ISR-safe
的模块级 sink 快回调（供 SAFE-1 拿零延迟故障信号）+ 观测面计数拉取（供
巡检/shell/未来 OBS-2）。`bm_tt_schedule` 在门控开启时是本模块的第一个宿主，
每个 LET 任务自动获得一个 span，无需业务代码手动打点。

设计全文见
[`docs/superpowers/specs/2026-07-02-safe2-wcet-mon-design.md`](../superpowers/specs/2026-07-02-safe2-wcet-mon-design.md)。

---

## 1. 核心抽象：`bm_wcet_span_t`

```c
typedef struct bm_wcet_span {
    /* 声明面（注册前由使用者填） */
    const char *name;          /* 诊断名 */
    uint32_t    budget_us;     /* WCET 预算；0 = 只计时不判超 */
    /* 内部状态（引擎读写，使用者不碰） */
    uint64_t    t0_us;         /* begin 时刻（bm_uptime_us） */
    uint8_t     running;       /* begin/end 配对状态 */
    /* 观测面（运行时累积，只读消费；计数饱和不回绕） */
    uint32_t    last_us;       /* 最近一次实测 */
    uint32_t    max_us;        /* 实测 WCET，单调增 */
    uint32_t    run_count;     /* 完成的 begin/end 对数 */
    uint32_t    overrun_count; /* 实测 > 预算 次数 */
    uint32_t    miss_count;    /* 外部上报 deadline miss 次数 */
    uint32_t    misuse_count;  /* begin/end 配对错误次数 */
} bm_wcet_span_t;
```

| 字段 | 面 | 含义 |
|---|---|---|
| `name` | 声明 | 诊断名，注册前由使用者填 |
| `budget_us` | 声明 | WCET 预算（µs）；`0` = 只计时、永不判超 |
| `t0_us` | 内部 | `begin` 记录的起始时刻（`bm_uptime_us()`），引擎读写，使用者不碰 |
| `running` | 内部 | `begin`/`end` 配对状态标志 |
| `last_us` | 观测 | 最近一次 `end` 实测耗时 |
| `max_us` | 观测 | 实测 WCET，单调增（历史最大值） |
| `run_count` | 观测 | 完成的 `begin`/`end` 配对次数（饱和不回绕） |
| `overrun_count` | 观测 | 实测 > `budget_us` 的次数（饱和不回绕） |
| `miss_count` | 观测 | 外部经 `bm_wcet_mon_report_miss` 上报的 deadline miss 次数（饱和不回绕） |
| `misuse_count` | 观测 | `begin`/`end` 配对错误次数（饱和不回绕） |

**声明宏**（一行声明，与手写结构体初始化等价）：

```c
#define BM_WCET_SPAN_DEFINE(id, budget_us_) \
    bm_wcet_span_t id = { .name = #id, .budget_us = (budget_us_) }
```

span 是**静态实例，零动态分配**——app/宿主自有存储，本模块只记录指针。

---

## 2. API 一览

| API | 语义 | 错误码 |
|---|---|---|
| `void bm_wcet_mon_init(void)` | 清空注册表与 sink 绑定，须在任何 `register`/`begin`/`end` 之前于 init 阶段（运行前单线程窗口）调用一次；重复调用等价于重置（供测试隔离）；顺带预热调用一次 `bm_uptime_us()`，把 uptime 后端懒初始化收口在 init 期，避免 ISR 内首调承担初始化开销 | 无返回值 |
| `int bm_wcet_mon_register(bm_wcet_span_t *span)` | 仅限 init 阶段调用，把 span 挂进定长全局指针表（容量 `BM_CONFIG_WCET_MON_MAX_SPANS`，缺省 16）供迭代访问；**begin/end 不要求先注册**——未注册段照常计时判超，只是不进迭代面 | `BM_OK` 成功；`BM_ERR_INVALID` span 为 NULL；`BM_ERR_ALREADY` 已注册过；`BM_ERR_NO_MEM` 注册表已满 |
| `void bm_wcet_mon_set_sink(bm_wcet_sink_fn fn, void *user)` | 仅限 init 阶段调用（`fn`/`user` 非原子对，运行期热换存在竞态窗口）；设置模块级 sink 快回调；传 `NULL` 关闭上报，此时仅观测面计数仍会累积 | 无返回值 |
| `void bm_wcet_mon_begin(bm_wcet_span_t *span)` | 记 `t0_us = bm_uptime_us()`，置 `running`；`span` 为 `NULL` 时直接返回无副作用；已 `running` 再 `begin`（重复 begin）→ `misuse_count++` 且覆盖 `t0_us`（鲁棒优先，不拒绝） | 无返回值（错误经 `misuse_count` 体现） |
| `void bm_wcet_mon_end(bm_wcet_span_t *span)` | 用当前 `bm_uptime_us()` 与 `t0_us` 算出 `elapsed`（64 位差、钳到 `UINT32_MAX`），更新 `last_us`/`max_us`/`run_count`；`budget_us > 0 && elapsed > budget_us` 时 `overrun_count++` 并经 sink 上报 `BM_WCET_EVT_BUDGET_OVERRUN`；`span` 为 `NULL` 时直接返回；未配对 `begin` 即调用 `end`（`running` 为假）→ 仅 `misuse_count++`，不更新任何统计、不触发 sink | 无返回值（错误经 `misuse_count` 体现） |
| `void bm_wcet_mon_report_miss(bm_wcet_span_t *span)` | 供宿主显式上报一次 deadline miss（该跑没跑成，`begin`/`end` 覆盖不到的场景）；`miss_count++` 并经 sink 上报 `BM_WCET_EVT_DEADLINE_MISS`（`measured_us=0`）；`span` 为 `NULL` 时直接返回 | 无返回值 |
| `uint32_t bm_wcet_mon_span_count(void)` | 查询当前已成功注册的监控段个数 | — |
| `const bm_wcet_span_t *bm_wcet_mon_span_at(uint32_t idx)` | 按注册顺序索引访问；`idx` 越界（`[0, span_count())` 之外）返回 `NULL` | — |

**并发契约**：每 span **单写者**（归属其执行上下文：ISR 域任务 span 只在
hrt ISR 写、MAINLOOP 域任务 span 只在主循环写、手动包段归调用者上下文）；
`register`/`set_sink` **仅限 init 阶段**（运行前单线程窗口）；观测面全为
32 位对齐字段，单核 ISR 写 / 主循环读无撕裂、无锁无临界区。注册表是**全局
单例**（SMP 共享地址空间，不假称 per-core），跨核聚合是上层（OBS-2/shell）
的事。

---

## 3. 两类事件语义辨析（勿混）

| 事件 | 触发条件 | 语义 |
|---|---|---|
| `BM_WCET_EVT_BUDGET_OVERRUN` | `bm_wcet_mon_end` 内实测 `elapsed > budget_us`（`budget_us > 0`） | 这段代码**跑了但超时**——WCET 违约 |
| `BM_WCET_EVT_DEADLINE_MISS` | 宿主显式调用 `bm_wcet_mon_report_miss` | 这段代码**该跑没跑成**（上一拍还没完、被 skip）——deadline 违约。`bm_tt_schedule` 的两处 skip 分支（ISR reentry / MAINLOOP pending 未消化）是天然上报点 |

`measured_us`：`BUDGET_OVERRUN` 时 = 实测 `elapsed`；`DEADLINE_MISS` 时
= `0`（没跑，无实测）。

---

## 4. sink 契约

```c
typedef void (*bm_wcet_sink_fn)(const bm_wcet_span_t *span, bm_wcet_evt_t evt,
                                uint32_t measured_us, void *user);
```

- **短、非阻塞、ISR-safe**：sink 可能在检测处上下文直接调用（可能是 hrt
  ISR），必须能在 ISR 里安全跑完，不得阻塞。
- **可重入且可并发**：单核场景下 ISR 可打断主循环中的 sink 再触发另一
  span 的 sink；多核场景下两核可能同时触发同一 sink——sink 实现自身须
  对此并发场景负责。
- **禁在 sink 内对同 span `begin`/`end`**（避免自触发递归）。
- **一个模块级 sink，不做 per-span 回调**（YAGNI；真需要 per-span 分流
  时 sink 自己按 `span` 指针分派）。
- **仅限 init 阶段设置**：`bm_wcet_mon_set_sink` 与 `register` 同约束，
  运行期不保证热换的可见性一致。
- 与 `bm_hrt_deadline_missed_hook` 既有风格一致。

---

## 5. 墙钟语义诚实标注

实测的 `elapsed` 是**墙钟时长**，单核场景下没有 per-context 运行时钟，因此：

- **ISR 域** span 在 hrt ISR 内测得，约等于纯执行时长（同级不被抢占；
  嵌套中断平台除外）。
- **MAINLOOP 域** span 的 `elapsed` **含被 hrt ISR 抢占的时间**——其预算
  判定语义是"响应时长超预算"，而不是"CPU 时长超预算"。这对 MAINLOOP
  反而更贴近 deadline 的本质（关心"何时完成"），但 SAFE-1 写判据、
  OBS-2 做对账时必须知道这一语义差异，不能把 MAINLOOP 的 `overrun_count`
  简单等同于纯 CPU 超时。

`bm_uptime_us()` 是 64 位单调时钟，无回绕论证负担；`elapsed` 64 位差钳到
`UINT32_MAX` 存入 32 位字段。每次打点（一对 begin/end）= 2 次
`bm_uptime_us()` 调用 + 常数算术与分支，作为框架开销计入 tt spec §5.4
"+框架µs"档；不可接受时整体关闭 `BM_ENABLE_WCET_MON`。

---

## 6. TT 自动接入与两个门控开关

`bm_tt_schedule` 是本模块的第一个宿主：**两个独立旋钮**，照
`BM_ENABLE_TT_SCHEDULE`/`BM_CONFIG_ENABLE_TT_SCHED` 既有范式：

| 开关 | 层级 | 作用 |
|---|---|---|
| `BM_ENABLE_WCET_MON`（CMake option，缺省 OFF） | 构建门控 | 决定是否编译 `bm_wcet_mon` 库；`bm_tt_schedule` 仅在 `BM_ENABLE_TT_SCHEDULE AND BM_ENABLE_WCET_MON` **两开关同开**时 PRIVATE 链接 `bm_wcet_mon` 并启用打点（编译定义 `BM_TT_SCHED_WCET_MON=1`） |
| `BM_CONFIG_ENABLE_WCET_MON`（`bm_config.h`，缺省 0） | umbrella 头裁剪 | `bm_hybrid.h` 按 `#if BM_CONFIG_ENABLE_WCET_MON` 条件 include 公共头 |

**门关时行为**：`bm_tt_schedule` 打点编译为空，行为与不接入本模块前
**完全一致**、零新符号（回归口径，见 spec §8 A4）。

**接入细节**（无需业务代码干预，`bm_tt_schedule.h` 公共结构零改动）：

- span 池藏在 `bm_tt_schedule.c` 内部（编译单元级 `static` 数组，容量
  与 `BM_CONFIG_WCET_MON_MAX_SPANS` 同界）；`bm_tt_schedule_init` 按任务
  表 entries 顺序分配 `span.name = activity name`、`span.budget_us =
  声明 wcet_us` 并调用 `bm_wcet_mon_register`。
- 打点位置：ISR 域派发 `running=1 → step → running=0` 之间、MAINLOOP 域
  `bm_tt_schedule_run_pending` 内 step 前后，均以 `begin`/`end` 包住
  step；ISR 域 reentry skip 分支与 MAINLOOP 域 pending 未消化 skip 分支
  各追加一次 `bm_wcet_mon_report_miss`。
- **双账并存是有意的**：门面自身的 `rt->overrun_count`（局部、始终在）
  与 span 的 `miss_count`（中央、门控在）各记各的——门面账喂 report/
  自检，span 账喂 SAFE-1/OBS-2。
- 重复 `bm_tt_schedule_init` 同一张表会复用既有映射条目并复位观测面
  （幂等，`BM_ERR_ALREADY` 视为成功）；池/注册表耗尽时 `init` 直接返回
  `BM_ERR_NO_MEM`（宁可失败，不悄悄不监控），多表部署需自行调大
  `BM_CONFIG_WCET_MON_MAX_SPANS`。

---

## 7. 手动包段示例

`BM_ENABLE_WCET_MON=OFF` 构建下手动包段的 app 自担适配（自行门控或恒开
本模块）——框架不提供"关门时的空壳宏"（YAGNI）。

```c
static BM_WCET_SPAN_DEFINE(sp_filter, 200u); /* 预算 200us */

/* init 阶段 */
bm_wcet_mon_register(&sp_filter);

/* 热路径 */
bm_wcet_mon_begin(&sp_filter);
run_filter();
bm_wcet_mon_end(&sp_filter);
```

---

## 8. 错误处理

| 情形 | 行为 |
|---|---|
| `register(NULL)` | `BM_ERR_INVALID` |
| 重复注册 | `BM_ERR_ALREADY` |
| 注册表满 | `BM_ERR_NO_MEM` |
| `end` 无匹配 `begin` | `misuse_count++`，不更新统计、不触发 sink（不崩不误报） |
| `running` 中再 `begin` | `misuse_count++` + 覆盖 `t0_us`（鲁棒优先） |
| `begin`/`end`/`report_miss(NULL)` | 直接返回，无副作用 |

---

## 9. 测试注意事项

native 单测里若要对 `begin`/`end` 做 µs 级精确耗时断言（如
`last_us`/`max_us` 是否恰好等于注入的时长），`setUp` 必须先
`bm_hal_uptime_native_reset()` 再调 `bm_hal_uptime_native_set_virtual(1)`
切到**纯虚拟时钟**——否则 `bm_uptime_us()` 仍叠加真实墙钟分量，`begin`
与 `end` 之间即便没有显式 `advance`，也会因真实时间流逝混入 0～1µs 的
抖动，导致精确断言偶发 flaky。参考现行写法（`tests/unit/test_wcet_mon.c`、
`tests/unit/test_tt_wcet.c`）：

```c
void setUp(void) {
    bm_hal_uptime_native_reset();
    bm_hal_uptime_native_set_virtual(1);
    bm_wcet_mon_init();
    /* ... */
}
```

`bm_hal_uptime_native_advance_us()` 仍是唯一推进虚拟时钟的手段——用它
在 `begin`/`end` 之间注入确定的耗时，杜绝真实忙等（详见
[`bm_hal_uptime_native.h`](../../portable/sim/native/bm_hal_uptime_native.h)）。

---

## 10. 相关文档

- TT 门面（第一个宿主）：[bm_tt_schedule.md](bm_tt_schedule.md)
- `bm_hrt`（`bm_hrt_deadline_missed_hook` 既有风格参照）：[bm_hrt.md](bm_hrt.md)
- 设计全文：[`docs/superpowers/specs/2026-07-02-safe2-wcet-mon-design.md`](../superpowers/specs/2026-07-02-safe2-wcet-mon-design.md)
