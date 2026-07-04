# bm_tt_schedule — 时间触发调度门面（TT/LET）

头文件：`include/bm/hybrid/bm_tt_schedule.h`
实现：`Source/hybrid/bm_tt_schedule.c`

## 概述

`bm_tt_schedule` 是框架的"第二张脸"：不同于 [bm_exec](bm_exec.md)/[bm_hrt](bm_hrt.md)
面向"槽位 + run 回调"的通用混合域编排，本门面面向**时间触发（TT）+
逻辑执行时间（LET）**这一类需求——开发者只写**纯函数** step，声明
一张静态**任务表**（周期/相位/输入输出绑定），门面负责：

- 周期头统一冻结全部输入（快照 + seq-delta 判龄，写 `stale`/`age`）；
- step 只读快照、只写自己的输出缓冲，不直接碰 bus；
- 周期尾把上一次已完成的结果按**双缓冲边界发布**到输出 bus（+1 个
  任务周期的确定性延迟，换取无锁）；
- ISR 域与 MAINLOOP 域统一走同一张表、同一个派发器，互不干扰。

**不做什么**：不做真正的抢占式多任务调度，不做动态优先级，不做
跨核通讯（跨核仍是每核各自一份实例，见头文件 `@core_affinity`）。

---

## 1. 核心概念

| 术语 | 含义 |
|------|------|
| **任务表 / activity** | `BM_LET_DEFINE_ISR`/`BM_LET_DEFINE_MAINLOOP` 声明的一行：周期(`every`×`minor_us`)、相位(`at`)、`domain`、`wcet_us`、纯函数 `step`、输入/输出绑定表 |
| **调度表 / schedule** | `BM_SCHEDULE_DEFINE` 声明：`minor_us`（时间格粒度）+ 若干 activity 指针；`n_frames = LCM(每任务的 every)`，由 `bm_tt_schedule_init` 算出 |
| **kind** | 任务轴之一，本轮仅 `BM_TT_KIND_COMPUTE` |
| **domain** | 任务轴之一：`BM_TT_DOMAIN_ISR`（`BM_LET_DEFINE_ISR`，ISR 内同步跑完）/ `BM_TT_DOMAIN_MAINLOOP`（`BM_LET_DEFINE_MAINLOOP`，ISR 只冻结挂起，主循环执行）——两个具名宏都是内部通用宏 `BM_LET_DEFINE_EX` 的薄包装，domain 由宏名显式区分，无隐藏缺省 |
| **LET 语义** | step 读到的输入是**周期头冻结的快照**，写的输出要到**周期尾**（下一次命中拍）才对外可见——纯函数、无副作用、逻辑执行时间与物理执行时间解耦 |

---

## 2. 声明一张任务表

```c
/* 输入绑定：源 bus + 保质期 + 安全值 */
static const bm_let_input_t g_speed_inputs[] = {
    { .bus = &g_current_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(float), .safe_default = &k_current_safe },
};
/* 输出绑定：目标 bus + 安全值 */
static const bm_let_output_t g_speed_outputs[] = {
    { .bus = &g_duty_bus, .elem_size = sizeof(float),
      .safe_default = &k_duty_safe },
};

/* 纯函数 step：只经 bm_let_in/bm_let_out 读写，不直接碰 bus */
static void speed_step(bm_let_ctx_t *ctx, void *state) {
    int stale;
    uint32_t age_us;
    const float *current = (const float *)bm_let_in(ctx, 0u, &stale, &age_us);
    float *duty = (float *)bm_let_out(ctx, 0u);

    *duty = stale ? 0.0f /* fail-safe：数据过期就不出力 */
                  : pi_update(&g_pi, *current);
}

/* every=1,at=0：每 minor_us 跑一次；wcet_us=80：声明的最坏执行时间 */
BM_LET_DEFINE_ISR(task_speed, 1u, 0u, 80u, speed_step, NULL,
                   g_speed_inputs, g_speed_outputs);

/* minor_us=1000：时间格粒度 1ms；可挂多个 activity，周期用 every 表达倍数关系 */
BM_SCHEDULE_DEFINE(sched_axis, 1000u, &task_speed);
```

- `BM_LET_DEFINE_ISR`/`BM_LET_DEFINE_MAINLOOP` 一行分配 snapshot（输入快照
  区）、输出双缓冲、per-input 运行态（`miss`/`stale`/`age_us`/
  `baseline_seq`）——全部编译期静态数组，开发者不用管 bookkeeping。
- `domain` 由宏名显式区分，无隐藏缺省：小计算量/硬实时用
  `BM_LET_DEFINE_ISR`（ISR 内同步跑完，`step` 必须短）；重计算/耗时不确定
  用 `BM_LET_DEFINE_MAINLOOP`（ISR 只冻结挂起，`step` 延后到主循环
  `bm_tt_schedule_run_pending` 里跑，见 §4）。两个宏都是内部通用宏
  `BM_LET_DEFINE_EX(id, domain, ...)` 的薄包装，仅 domain 固化不同。
- `bm_let_in(ctx, idx, &stale, &age_us)`：读第 `idx` 个输入的冻结快照，
  同时拿到本次判龄结果；`bm_let_out(ctx, idx)`：拿第 `idx` 个输出的
  写指针（写的是"当前处理中"那份缓冲，不是马上就发布的那份）。
- `max_age_us` 填 `BM_LET_AGE_DEFAULT`（如上例）表示不显式指定保质期，
  由门面在 `init` 期解析为 **2×本任务周期**（`period_us = minor_us ×
  every`）；需要更严格/更宽松的保质期时，直接填一个具体的微秒数覆盖
  默认值（见 §7）。

---

## 3. 初始化与 HRT 接线

```c
if (bm_tt_schedule_init(&sched_axis) != BM_OK) {
    /* 参数/周期不一致、LCM(every) 超 BM_CONFIG_TT_SCHED_MAX_FRAMES、
     * 或某 minor 格内 ISR 域 wcet 之和超载——拒绝装配，不允许带病启动 */
}

/* period_us=minor_us，callback 经门面内部蹦床转 bm_tt_schedule_tick；
 * 一张调度表对应一个 hrt slot，多张表就多份 slot 一起喂 bm_hrt_init */
bm_hrt_slot_t g_hrt_slots[2];
g_hrt_slots[0] = bm_tt_schedule_hrt_slot(&sched_axis);
g_hrt_slots[1] = bm_tt_schedule_hrt_slot(&sched_bms);
bm_hrt_init(g_hrt_slots, 2u);
bm_hrt_start();
```

`bm_tt_schedule_init` 成功后，**任何 tick 之前**下游 bus 已经能读到各
输出的 `safe_default`（即"预发布"）；`rt` 全部状态复位。**第一次真实
tick 之后发布的仍是 safe_default**（+1 拍延迟对首拍同样成立，不会因为
init 预填就提前把 step 刚算出的结果发出去）——这是本门面对"首拍必须
安全"这一红线的具体落地，业务侧不需要额外加判断。

---

## 4. MAINLOOP 域接线：`bm_tt_schedule_run_pending`

`domain=BM_TT_DOMAIN_ISR` 的任务在 ISR 里同步跑完；
`domain=BM_TT_DOMAIN_MAINLOOP` 的任务在 ISR 里**只冻结输入、置
pending**，真正执行的 step 交给主循环调用 `bm_tt_schedule_run_pending`
——这一段是**有界 drain**，与 [`bm_exec_drain_streams`](bm_exec.md)、
`bm_event_process` 属于同一类"主循环里跑一段有预算的活"：

```c
for (;;) {
    bm_exec_drain_streams(4);                  /* Block/Frame 槽 */
    (void)bm_tt_schedule_run_pending(&sched_axis, 4u); /* MAINLOOP 域 LET 任务 */
    bm_event_process(8);
    bm_wdg_feed();
}
```

- `budget` 是本次最多跑几个待处理任务，超出预算的留到下次主循环圈
  再跑——同样是"有界"，不会因为攒了很多待处理任务就让本圈失控变长。
- `run_pending` 本身**不发布**结果——它只是把 step 跑完、翻转双缓冲、
  置 `fresh=1`；真正的发布延后到下一次 ISR tick 里由派发器统一按
  "先发布上一拍结果、再冻结/挂起本拍"的顺序完成。这保证了 MAINLOOP
  域任务同样满足"+1 拍延迟发布"的 LET 语义，不会因为主循环节奏不稳定
  而破坏发布时机的确定性。
- **若主循环迟迟不调 `run_pending`**：ISR 侧发现 `pending` 仍为真（上
  一拍冻结的输入还没被消化），会记 `overrun_count+1` 并**放弃**（不
  重新冻结、不覆盖已冻结的快照），该任务对应的输出 bus **seq 严格
  不变**——"主循环饿死"是外部可观测、可诊断的（读 `overrun_count`
  或直接比对 bus seq），不会静默丢数据或用半新半旧的输入拼出脏结果。

---

## 5. domain 选择指南

| 场景 | 选 `ISR` | 选 `MAINLOOP` |
|------|----------|----------------|
| 计算量小、有硬实时截止（电流环、速度环） | ✓ | |
| 计算量大/耗时不确定（滤波器整定、诊断统计、日志格式化） | | ✓ |
| 需要调用非 ISR-safe API（`bm_event_process`、复杂日志） | | ✓ |
| 每拍必须完成、不能被主循环节奏拖慢 | ✓ | |

**进 ISR 域的 step 必须短**：`wcet_us` 是需要如实声明的**上界**，
`bm_tt_schedule_init` 用它做节拍负载校验（`tt_frame_check`：同一 minor
格内所有 ISR 域任务 wcet 之和必须 ≤ `minor_us`，超了直接拒绝
init）——声明不实会让这层保护形同虚设，故声明值应来自真机实测或
保守估算，不能拍脑袋填一个偏小的数字。

---

## 6. 红线（不可违反的设计约束）

1. **step 必须是纯函数**：只经 `bm_let_in`/`bm_let_out` 读写，不直接
   访问 bus、不访问其他任务的状态、不做阻塞调用。这是"逻辑执行时间"
   与"物理执行时间"解耦的前提，也是本门面能做静态确定性分析
   （零动态分配、有界循环）的前提。
2. **fail-safe 优先于新鲜度**：输入 `stale=1`（从未发布，或 age 超过
   `max_age_us`）时，业务逻辑应主动降级（如上面 `speed_step` 例子里
   `stale` 时输出 0 占空比），而不是假装数据仍然新鲜继续用。安全默认值
   `safe_default` 是**非 NULL 强制项**（`BM_LET_DEFINE_ISR`/`BM_LET_DEFINE_MAINLOOP`/`init` 校验），
   不允许留空。
3. **overrun 不雪崩**：ISR 域 reentry（上一拍还没跑完）与 MAINLOOP 域
   pending 未消化，都只是**整体跳过本次 + 计数**，不重试、不排队补
   偿、不让下一拍继续累积压力——一次过载只会丢一拍的产出，不会连锁
   放大成系统性故障。

---

## 7. seq-delta age：周期量化近似，非精确 µs

`bm_let_in` 返回的 `age_us` 是 **`miss × 任务周期`**（`miss` 是"距离上次
输入 bus 的 seq 发生变化，已经过了几个本任务周期"），**不是**输入数据
真实产生时刻到当前的精确微秒差。原因：门面走的是无锁 `bm_bus` LATEST
的 seqlock 快照 + seq 比对，代价是拿不到"生产者具体在哪个绝对时刻写
入"的时间戳——只知道"跟上次冻结时相比，seq 变没变"。

实践含义：

- `max_age_us` 应按**任务周期的整数倍**设置才有意义（如 `2×period`），
  设成介于两个周期之间的数值不会带来更精细的判定粒度。填
  `BM_LET_AGE_DEFAULT`（见 §2）即取默认的 `2×period`，不需要手算。
- 生产者比消费者**快**（每拍都发新值）：只要两次冻结之间 seq 变过，
  `miss` 恒为 0、`age_us` 恒为 0——这不代表"绝对零延迟"，只代表"每次
  冻结都拿到了自上次以来的最新值"。
- 生产者比消费者**慢**：`miss` 按"漏跳了几个消费周期"累加，`age_us`
  随之是该周期数的粗粒度量化，不是生产者与消费者之间的真实时间差。

需要真实微秒级新鲜度时，应用侧应在 payload 里自带时间戳（如
`bm_uptime_now_us()`），由 step 自行比对——门面本身不提供这一层。

---

## 8. 诊断：`bm_tt_schedule_report` / RTA 导出

```c
static void print_line(const char *line, void *u) { (void)u; puts(line); }
bm_tt_schedule_report(&sched_axis, print_line, NULL);
```

输出两块：ISR 域·时间格视图（每 minor 格 Σ 命中任务 `wcet_us`，标出
峰值格是否 `≤ minor_us`，表头明确标注"声明 wcet_us · 计划视图"这一
时间来源，与真机实测区分开）+ MAINLOOP 域·预算账（逐任务 `wcet_us` +
建议 `run_pending` budget）。

`bm_tt_schedule_rt_slot_count`/`bm_tt_schedule_rt_slot_at` 导出 RTA
中立只读描述符（`owner_cpu`/`kind`/`domain`/`wcet_us`/`period_us`/
`deadline_us`），只覆盖 ISR 域任务（MAINLOOP 域无硬时间格语义），供
外部 RTA/调度分析工具消费，门面本身不 include 该类工具的头文件。

---

## 9. JSON 导出：`bm_tt_schedule_report_json`

`bm_tt_schedule_report` 是给人看的文本报告；`bm_tt_schedule_report_json`
是给**工具**看的机读报告（schema v1），逐行经 `emit` 回调发出一个 JSON
对象，字段顺序固定、内容确定性（同一调度表状态永远输出逐字节相同的
结果）。用途：喂给 `tools/schedule_map_tool.py` 做多核/跨核复合分析（见
[../01-应用开发/06-调度表导出schedule-map](../01-应用开发/06-调度表导出schedule-map.md)）。

```c
void bm_tt_schedule_report_json(const bm_tt_schedule_t *sched,
                                 const bm_tt_schedule_json_meta_t *meta,
                                 void (*emit)(const char *line, void *u), void *u);
```

- `sched`：只读调度表实例。
- `meta`：调用方提供的导出元信息，**传 `NULL` 等价于全零**（`cpu=0`、
  `ref_clk_hz=0`、无工作点）——不会因为调用方偷懒不传而报错，只是频率
  缩放这类需要参考时钟的分析会被下游工具跳过并 WARN。
- `emit`/`u`：与 `bm_tt_schedule_report` 相同的逐行输出约定。

### 9.1 `bm_tt_schedule_json_meta_t`

| 字段 | 类型 | 含义 |
|------|------|------|
| `cpu` | `uint8_t` | 本表所属 CPU 编号；单核填 0 |
| `ref_clk_hz` | `uint32_t` | 声明 `wcet_us` 时所依据的参考时钟频率；0=未声明（下游频率缩放分析跳过） |
| `operating_points_hz` | `const uint32_t *` | 工作点频率数组，可为 `NULL` |
| `operating_point_count` | `uint8_t` | 工作点个数 |

### 9.2 schema v1 字段表

顶层对象：

| 字段 | 类型 | 语义 |
|------|------|------|
| `schema_version` | int | 恒为 `1` |
| `sched_name` | string | `BM_SCHEDULE_DEFINE` 的 `#id` |
| `cpu` | int | 取自 `meta->cpu` |
| `minor_us` | int | 时间格粒度 |
| `n_frames` | int | 超周期帧数，`LCM(每任务 every)` |
| `hyperperiod_us` | int | `minor_us × n_frames`（`uint64_t` 打印，避免大表溢出） |
| `overhead_us` | int | `BM_CONFIG_TT_SCHED_OVERHEAD_US`（每 minor 格框架派发开销声明值） |
| `overhead_calibrated` | bool | `BM_CONFIG_TT_SCHED_OVERHEAD_CALIBRATED`：`false`=占位估算，`true`=真机实测回填 |
| `ref_clk_hz` | int | 取自 `meta->ref_clk_hz` |
| `operating_points_hz` | int[] | 取自 `meta->operating_points_hz` |
| `tasks[]` | object[] | 逐 activity（顺序=`entries` 声明顺序） |
| `frames[]` | object[] | 逐 minor 格（`t` 升序 0..n_frames-1） |
| `edges[]` | object[] | 预留，本轮恒为空数组，未来任务填充 |

`tasks[]` 每项：

| 字段 | 语义 |
|------|------|
| `name` | 声明宏 `#id` 字符串化，恒为合法 C 标识符，天然免转义 |
| `every`/`at` | 分频/相位，同 `BM_LET_DEFINE_*` 参数 |
| `wcet_us` | 声明的最坏执行时间 |
| `domain` | `"isr"` / `"mainloop"` |
| `kind` | 本轮恒为 `"compute"` |
| `period_us`/`deadline_us` | 均为 `minor_us × every`（LET 语义：deadline 恒等于周期） |
| `inputs`/`outputs` | 输入/输出绑定数量 |

`frames[]` 每项：

| 字段 | 语义 |
|------|------|
| `t` | 第几个 minor 格（0..n_frames-1） |
| `isr_load_us` | 该拍命中的 ISR 域任务 `wcet_us` 之和 **+** `overhead_us`（框架派发开销计入 ISR 域账） |
| `mainloop_pending_us` | 该拍命中的 MAINLOOP 域任务 `wcet_us` 之和（静态计划视图，非运行时实测；不含 `overhead_us`——那是 ISR 域派发成本，与 MAINLOOP 无关） |

**`estimated` 语义**：JSON 本体只吐"声明值"（`wcet_us`/`overhead_us` 均
来自静态声明或 bm_config，不是运行时采样），任何跨工作点频率缩放得到
的数字都是下游工具（`schedule_map_tool.py`）算出来附加标注
`(estimated)` 的推算值，本 API 不做频率缩放,也不产出"estimated"字样。

### 9.3 三档场景下的最小导出思路

一档（native/CMake）走 `bm_add_schedule_map`（见 06 文档 §2），构建期
自动跑通 `report_json`；二/三档（Keil/IAR、真实 target）没有构建期跑
host 程序的条件时，可以在 target 固件里直接调用本 API，把 JSON 经串口
（UART）逐行吐出，PC 端抓下来存成 `.json` 文件再喂
`tools/schedule_map_tool.py`：

```c
static void uart_emit_line(const char *line, void *u) {
    (void)u;
    uart_write_line(line); /* 逐行发送，一行一次 uart_write + 换行 */
}

void debug_dump_schedule_json(void) {
    bm_tt_schedule_json_meta_t meta = {
        .cpu = 0u, .ref_clk_hz = 168000000u,
        .operating_points_hz = NULL, .operating_point_count = 0u,
    };
    bm_tt_schedule_report_json(&sched_axis, &meta, uart_emit_line, NULL);
}
```

详细的二/三档抓取步骤见
[../01-应用开发/06-调度表导出schedule-map](../01-应用开发/06-调度表导出schedule-map.md) §5。

### 9.4 注册单元契约（`bm_schedule_map_reg.h`）

`tools/schedule_map/bm_schedule_map_reg.h` 定义的是"注册单元"（register
unit）——一个只列清单、不含业务逻辑的小翻译单元，通用 dump 程序
`bm_schedule_map_main.c` 只认这七个符号，不认识任何具体调度表名字：

```c
typedef struct {
    bm_tt_schedule_t *sched; /* 表实例（extern 引用应用装配文件里的静态实例） */
    uint8_t            cpu;   /* 表所属 CPU */
} bm_schedule_map_entry_t;

extern const bm_schedule_map_entry_t g_bm_schedule_map_entries[];
extern const uint32_t g_bm_schedule_map_entry_count;
extern const uint32_t g_bm_schedule_map_ref_clk_hz;
extern const uint32_t g_bm_schedule_map_op_points_hz[];
extern const uint32_t g_bm_schedule_map_op_point_count;

/* 分析专用干扰源声明（HRT 抢占上界，供 schedule-map-tool 算有效峰值），
 * 可为空——未声明干扰源时数组填单个占位元素、count 填 0 即可，见下方样例 */
extern const bm_schedule_map_interference_t g_bm_schedule_map_interference[];
extern const uint32_t g_bm_schedule_map_interference_count;

int bm_schedule_map_setup(void); /* init 前的准备工作（如 bm_bus_open） */
```

`bm_add_schedule_map()`（见 06 文档 §2）会自动生成满足该契约的 `.c`；
IDE 二/三档没有 CMake 时可手写，等价样例见
`tests/tools/schedule_map_fixture_reg.c`：

```c
#include "bm_schedule_map_reg.h"
#include "schedule_map_fixture.h"

const bm_schedule_map_entry_t g_bm_schedule_map_entries[] = {
    { &sched_fixture_a, 0u },
    { &sched_fixture_b, 1u },
};
const uint32_t g_bm_schedule_map_entry_count = 2u;
const uint32_t g_bm_schedule_map_ref_clk_hz  = 240000000u;
const uint32_t g_bm_schedule_map_op_points_hz[] = { 240000000u, 80000000u };
const uint32_t g_bm_schedule_map_op_point_count = 2u;

#ifdef BM_CONFIG_SM_INTERFERENCE_SRC
/* config 单源：应用工程推荐写法（bm_config_app.h 定义宏后此分支生效） */
const bm_schedule_map_interference_t g_bm_schedule_map_interference[] =
    BM_CONFIG_SM_INTERFERENCE_SRC;
const uint32_t g_bm_schedule_map_interference_count =
    (uint32_t)(sizeof(g_bm_schedule_map_interference) / sizeof(g_bm_schedule_map_interference[0]));
#else
/* 空占位：未声明干扰源时按此定义即可满足链接期契约 */
const bm_schedule_map_interference_t g_bm_schedule_map_interference[] = { { { "", 0u, 0u, 0u }, 0u } };
const uint32_t g_bm_schedule_map_interference_count = 0u;
#endif

int bm_schedule_map_setup(void) { return schedule_map_fixture_setup(); }
```

干扰两符号的定义处与 `tests/tools/schedule_map_fixture_reg.c` 逐字同款
——手写注册单元也遵循"config 单源"约定：IDE 档用户也只动
`bm_config_app.h`（定义/不定义 `BM_CONFIG_SM_INTERFERENCE_SRC`），不需要
改这份 `.c` 本身。

干扰源声明特性的完整迁移说明（含 JSON 字段、`bm_add_schedule_map`
自动兼容细节）见
[06-调度表导出schedule-map §2.x 迁移说明](../01-应用开发/06-调度表导出schedule-map.md)。

## 10. 错误码

| 码 | 场景 |
|----|------|
| `BM_ERR_INVALID` | `minor_us`/`entry_count`/`every`/`at`/`input_count`/`elem_size` 越界；`safe_default` 为空；`LCM(every)` 超 `BM_CONFIG_TT_SCHED_MAX_FRAMES`；某 minor 格 ISR 域 wcet 之和超载；`rt_slot_at` 索引越界 |

## 11. 相关文档

- 混合域接线总览：[../01-应用开发/05-混合域接线](../01-应用开发/05-混合域接线.md)
- 调度表导出三档接入：[../01-应用开发/06-调度表导出schedule-map](../01-应用开发/06-调度表导出schedule-map.md)
- `bm_hrt`：[bm_hrt.md](bm_hrt.md)
- `bm_exec`（`bm_exec_drain_streams` 主循环并列写法）：[bm_exec.md](bm_exec.md)
