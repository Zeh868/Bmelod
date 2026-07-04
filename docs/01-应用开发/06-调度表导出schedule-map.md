# 06 调度表导出 schedule-map

> **本文职责**：`bm_tt_schedule` 装配文件的编写约定、一档/二档/三档如何把调度表
> 导出成可读/机读产物、级 2 复合分析工具与 HTML 可视化的用法、DVFS 立场。  
> **不负责**：`BM_LET_DEFINE_*`/LET 语义本身 → [bm_tt_schedule API](../05-API参考/bm_tt_schedule.md)。

调度表导出（schedule-map）解决的问题：一张 `BM_SCHEDULE_DEFINE` 声明的
静态任务表，光看源码很难一眼确认"超周期多长、每个 minor 格挤了多少
`wcet_us`、会不会超载"。schedule-map 把这张表**在构建期或真机端直接
导出**成人读文本 / 机读 JSON，再喂给统一的级 2 Python 工具做多核并列
总表、全局超周期、频率缩放告警，以及自包含离线可看的 HTML 可视化。

---

## 1. 装配文件约定

**为什么要单独拆一个"装配文件"**：`BM_SCHEDULE_DEFINE`/`BM_LET_DEFINE_*`
声明的 bus/任务表是纯逻辑对象，不依赖任何具体硬件；把它们放进一个专门
的 `.c`（而不是散落在 `main.c` 或直接和 HAL 代码混在一起），换来的是
**同一份装配文件可以被两种完全不同的宿主复用**而不用改一行：

1. 真实 target 主构建（`main.c` 里 `bm_hrt_slot_t` + ISR 驱动
   `bm_tt_schedule_tick()`）；
2. `bm_add_schedule_map()` / 二档脚本拉起的宿主 dump 程序（只调
   `bm_tt_schedule_init` + `report`/`report_json`，从不真正跑 tick）。

装配文件清单（缺一条都不满足"可被 dump 程序复用"这个前提）：

- **零硬件 include**：不 `#include` 任何 HAL/驱动头，只依赖
  `bm_tt_schedule.h`/`bm_bus.h` 这类纯逻辑头。
- **`setup` 函数返回 `int`**：`0` 成功、非零失败（通常是某个
  `bm_bus_open` 失败），签名形如 `int xxx_setup(void)`——这个函数名会被
  原样写进注册单元的 `bm_schedule_map_setup()` 转发（见
  [bm_tt_schedule API §9.4](../05-API参考/bm_tt_schedule.md#94-注册单元契约bm_schedule_map_regh)）。
- 调度表实例（`BM_SCHEDULE_DEFINE` 生成的变量）与所有 bus 全局对象都是
  文件级静态/全局，供外部 `extern` 引用。

**真实样例**：`Demo/tt_schedule_balance/balance_schedule.c`——`ctrl` 表
挂两个任务：`balance`（ISR 域，读 IMU pitch 算力矩指令，STALE 时输出
0 力矩 fail-safe）+ `telemetry`（MAINLOOP 域，对力矩指令做指数滑动平均，
示范"重计算任务路由到主循环而非 ISR 时间片"）。整份文件只
`#include "balance_schedule.h"` 与 `bm_bus.h`，`main.c` 手动 tick 循环
（native_sim）与真实 target 的 `bm_hrt` ISR 驱动是**唯一**的差异点，
装配文件本身在两种宿主下逐字节相同。

Hoverboard 平衡车工程的真实装配文件 `Source/control/hover_control_let.c`
遵循同一约定：零硬件 include、`setup` 返回 `int`，供该工程自己的
`bm_add_schedule_map()` 调用直接复用出表，不需要为 schedule-map 另写
一份"影子装配"。

---

## 2. 一档接入：`bm_add_schedule_map`

一档（native/CMake 主构建）只需一行函数调用（定义见
`cmake/bm_schedule_map.cmake`，参数表见
[../02-构建与工具链/01-CMake选项与bm_config](../02-构建与工具链/01-CMake选项与bm_config.md#bm_add_schedule_map一行接入调度表导出)）：

```cmake
include(${BMELOD_ROOT}/cmake/bm_schedule_map.cmake)
bm_add_schedule_map(tt_schedule_balance_map
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/balance_schedule.c
    SETUP   balance_schedule_setup
    TABLES  ctrl:0
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR})
```

构建结束（`POST_BUILD`）自动做两件事：跑 dump 可执行导出
`<OUTPUT_DIR>/<表名>.{txt,json}`，再跑 `tools/schedule_map_tool.py` 汇总
成 `<OUTPUT_DIR>/schedule_map_all.txt`。真实构建输出（`ctrl.txt`）：

```text
=== ctrl ISR 域·时间格视图 [时间来源: 声明 wcet_us · 计划视图] ===
  开销: 0us [未标定占位]
  ISR name=balance every=1 at=0 wcet_us=40
  峰值格: t=0, 本格us=40, ≤minor_us(1000) ✓
=== ctrl MAINLOOP 域·预算账 ===
  MAINLOOP name=telemetry wcet_us=200 run_pending_budget_hint=1
注: 本表仅含 TT 门面负载,账外中断/slot 不在内
```

对应的 `ctrl.json`（schema v1，字段语义见
[bm_tt_schedule API §9.2](../05-API参考/bm_tt_schedule.md#92-schema-v1-字段表)）：

```json
{
  "schema_version": 1,
  "sched_name": "ctrl",
  "cpu": 0,
  "minor_us": 1000,
  "n_frames": 10,
  "hyperperiod_us": 10000,
  "overhead_us": 0,
  "overhead_calibrated": false,
  "ref_clk_hz": 0,
  "operating_points_hz": [],
  "tasks": [
    {"name": "balance", "every": 1, "at": 0, "wcet_us": 40, "domain": "isr", "kind": "compute", "period_us": 1000, "deadline_us": 1000, "inputs": 1, "outputs": 1},
    {"name": "telemetry", "every": 10, "at": 0, "wcet_us": 200, "domain": "mainloop", "kind": "compute", "period_us": 10000, "deadline_us": 10000, "inputs": 1, "outputs": 1}
  ],
  "frames": [
    {"t": 0, "isr_load_us": 40, "mainloop_pending_us": 200},
    {"t": 1, "isr_load_us": 40, "mainloop_pending_us": 0}
  ],
  "edges": []
}
```
（`frames` 完整 10 条按 `t=0..9` 排列，此处省略中间重复行。）

多表/多核场景下（`tests/tools/schedule_map_fixture_reg.c` 的两张
fixture 表：`sched_fixture_a`@cpu0、`sched_fixture_b`@cpu1），
`schedule_map_all.txt` 是真实产物：

```text
=== schedule-map 复合报告 (schema v1) ===
[cpu0] sched_fixture_a: minor=1000us frames=10 hyper=10000us 开销=0us[未标定占位] 峰值格 t=0 (100us, 10.0% of minor)
  ISR      task_fxa_fast  every=1 at=0 wcet=50us period=1000us
  ISR      task_fxa_mid  every=5 at=0 wcet=50us period=5000us
  ISR      task_fxa_slow  every=10 at=9 wcet=50us period=10000us
  MAINLOOP task_fxa_tele  every=10 at=0 wcet=200us period=10000us
  OP @80000000Hz: est 峰值 300us / minor 1000us OK (estimated)
  注: 本表仅含 TT 门面负载，账外中断/slot 不在内
[cpu1] sched_fixture_b: minor=2000us frames=2 hyper=4000us 开销=0us[未标定占位] 峰值格 t=1 (80us, 4.0% of minor)
  ISR      task_fxb_solo  every=2 at=1 wcet=80us period=4000us
  OP @80000000Hz: est 峰值 240us / minor 2000us OK (estimated)
  注: 本表仅含 TT 门面负载，账外中断/slot 不在内
全局超周期: 20000us = LCM(10000, 4000)
```

若 `bm_tt_schedule_init` 因参数非法/超载失败，出表程序返回非 0，
`POST_BUILD` 步骤失败即构建失败——出表不通过就是硬 gate，不会带着一张
排不下的表继续往下构建。

**干扰源声明：`INTERFERENCE_SRC`（opt-in，计入 Hardware/Scheduled HRT
抢占）**：`bm_add_schedule_map` 的 `INTERFERENCE_SRC` 参数（可重复传多
条）声明"这张表所在 CPU 上，还有哪些不在这张调度表里、但会抢占它时间
格"的周期性活动——典型是另一路 Hardware HRT（PWM/ADC IRQ）或另一张表的
Scheduled HRT 任务：

```cmake
bm_add_schedule_map(tt_schedule_balance_map
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/balance_schedule.c
    SETUP   balance_schedule_setup
    TABLES  ctrl:0
    INTERFERENCE_SRC
        "adc_irq:1000:20:hardware:0"
        "axis_speed:5000:50:scheduled:0"
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR})
```

每条格式 `name:period_us:wcet_us:tier:cpu`：`name` 限 C 标识符字符集，
`tier` 只能是 `hardware` 或 `scheduled`，`cpu` 决定这条干扰源关联到哪些
表——同一 CPU 上的所有表共用该 CPU 的干扰源集合。格式非法的条目在配置
期直接 `FATAL_ERROR`，不静默吞掉（错配的干扰源比没声明更危险）。不传
`INTERFERENCE_SRC` 时生成空数组，与声明前的行为逐字一致（opt-in）。单
CPU 声明的干扰源超过 16 条（`BM_SM_MAX_INTF`）时，出表程序向 stderr
打告警并截断多出的条目——分析会低估干扰，需要精简声明或拆分 CPU。

对应 JSON 在 `operating_points_hz` 之后新增一个数组字段：

```json
"interference_sources": [
    {"name": "adc_irq", "period_us": 1000, "wcet_us": 20, "tier": "hardware"},
    {"name": "axis_speed", "period_us": 5000, "wcet_us": 50, "tier": "scheduled"}
]
```

未声明干扰源时该字段是空数组 `[]`。

---

## 3. 表怎么读

- **超周期 `hyperperiod_us`**：`minor_us × n_frames`，`n_frames` 是各
  任务 `every` 的 `LCM`——多久之后整张表的相位关系完整重复一轮。
- **拍（`frames[]`/时间格视图）**：每个 minor 格是一行，落在这一格上的
  任务由 `every`/`at` 决定（`t % every == at`）。
- **两域分账**：`isr_load_us` 只统计 ISR 域任务（+ 框架开销），
  `mainloop_pending_us` 只统计 MAINLOOP 域任务——两者**不相加**、不共用
  一个"总负载"数字，因为 ISR 域有硬时间格约束（`Σwcet ≤ minor_us`，
  `bm_tt_schedule_init` 强制校验），MAINLOOP 域只是"预算提示"，由主循环
  `bm_tt_schedule_run_pending(sched, budget)` 按需消化，没有硬时间格
  约束。
- **开销标注**：`开销=<us>[未标定占位|已标定]` 对应 JSON 的
  `overhead_us`/`overhead_calibrated`，来自
  `BM_CONFIG_TT_SCHED_OVERHEAD_US`/`BM_CONFIG_TT_SCHED_OVERHEAD_CALIBRATED`
  （见 [DVFS 立场 §6](#6-dvfs-立场)）。
- **`estimated` 语义**：本体 JSON/文本报告只吐"声明值"（静态
  `wcet_us`/`overhead_us`），从不做频率换算；任何标了 `(estimated)`
  字样的数字都是级 2 工具在**另一个工作点频率**下重新估算出的峰值,
  不是这张表在该频率下的实测结果——`OP @80000000Hz: est 峰值 300us ...
  (estimated) `一行就是这个意思：假设换到 80MHz 跑，声明的 wcet 按
  `ref_clk_hz/f_hz` 线性缩放后估计峰值格要多久。
- **账外免责行**：每张表末尾固定输出"注: 本表仅含 TT 门面负载,账外
  中断/slot 不在内"——`bm_tt_schedule` 只知道自己名下这些 activity 的
  声明 wcet，真机上同一核还跑着的其他 ISR/HAL 中断、`bm_exec` 槽位等
  **不计入这张表**，不能把这张表的负载百分比当成整核 CPU 占用率。
- **干扰计入（opt-in）**：声明了 `INTERFERENCE_SRC` 后，文本报告峰值格
  下方多一行 `干扰(硬X/调Y)=Zus  有效峰值=Wus (P% of minor) 排得下✓|
  超载✗`——`硬X`/`调Y` 是 `tier=hardware`/`tier=scheduled` 两部分干扰
  各自的 ceiling 上界之和，`有效峰值 = TT 门面峰值格 isr_load_us + 干扰
  合计`；判可行性用有效峰值而非裸峰值（`isr_load_us + I ≤ minor_us`）。
  多频率对比表在有干扰声明时把"峰值"列换成"有效(含干扰)"列（HTML）/
  `有效=Wus（含干扰Z）`（文本），同样按该频率档 `ref/f_hz` 缩放后的
  wcet 重算干扰。账外免责行也相应切换：有声明时读作"已计入声明干扰源
  （N 个 HRT 抢占，ceiling 上界；未声明的仍在账外）"，无声明时仍是上面
  这句原文，两种文案不会同时出现在同一张表下。
- **已知局限**：ceiling 上界按 `Σ ceil(minor_us/period_us) × wcet_us`
  静态估算，不考虑相位/抖动，偏悲观（安全方向，不会低估）；是保守的
  **全抢占假设**——所有声明的干扰源都当作会实际抢占计算，`tier` 只用于
  归因展示、不做门控放行；这份估算的可信度取决于声明是否与真实硬件/
  调度配置一致（声明者职责，框架不做交叉校验）；Hardware 干扰源的
  `wcet_us` 最终需要上板实测校准，构建期声明的只是估计值。

---

## 4. 级 2 工具与 HTML 可视化

`tools/schedule_map_tool.py` 吃 N 份 `schema_version=1` 的 JSON（一张表
一份文件），只做汇总呈现——单表事实（峰值格、超周期）不重算，跨表唯一
新增计算是"全局超周期 = LCM(各表 hyperperiod_us)"与工作点线性缩放
估算。

```text
用法: schedule_map_tool.py (--dir D | files...) [--out F] [--html F] [--op-hz N]... [--load-warn-pct 80]
退出码: 0=成功(允许 WARN)  2=schema/IO 错误
```

| 参数 | 说明 |
|------|------|
| `files`（位置参数） | 逐个指定 JSON 文件路径，可与 `--dir` 同用 |
| `--dir D` | 取 `D` 目录下所有 `*.json` 作为输入 |
| `--out F` | 把文本报告额外写一份到文件 `F`（stdout 总是打印一份） |
| `--html F` | 额外产出自包含可视化 HTML 到文件 `F` |
| `--op-hz N` | 追加一个工作点频率（可重复传多次），叠加进每张表的频率缩放估算 |
| `--load-warn-pct 80` | 峰值格占 `minor_us` 百分比超过该阈值即 WARN（默认 80） |

**HTML 可视化**：`--html F` 产出的是一份纯内联 `<style>`/`<svg>` 的单
文件 HTML，零外链（无 CDN、无远程字体/脚本），离线双击即可在浏览器里
打开，光底/暗底都可读（`prefers-color-scheme` 自适配）。每张表画三张图，
**只画 JSON 里已有的字段，不做任何图上的二次计算**：

1. **时间格视图**：一排 `n_frames` 个格子，按 `isr_load_us/minor_us`
   占比着色（正常/近阈值/超载三色），峰值格描边加粗高亮；
2. **每拍 WCET 图**：`isr_load_us` 柱状图 + `minor_us` 参考虚线，超线的
   柱子变红；
3. **负载图**：`isr_load_us` 与 `mainloop_pending_us` 堆叠柱 +
   `minor_us` 参考线，一眼看出 ISR/MAINLOOP 两域各自占了这一拍多少。

生成 HTML 的命令示例（对 fixture 双表跑一遍，同时叠加一个 80MHz 工作
点缩放估算）：

```bash
python tools/schedule_map_tool.py --dir build/schedule_map \
    --out build/schedule_map/schedule_map_all.txt \
    --html build/schedule_map/schedule_map_all.html \
    --op-hz 80000000
```

---

## 5. 二档 / 三档

### 5.1 二档：`tools/board/build_schedule_map.ps1`

没有主 CMake 工程（Keil MDK / IAR EWARM 走各自的工程文件）时，用这个
脚本借宿主默认工具链单独配置+构建 `cmake/schedule_map_host/` 这个迷你
子工程出表——与一档 `bm_add_schedule_map()` 在交叉编译下自动走的是
**同一个**宿主子构建模板，只是触发方式换成命令行，方便挂进 Keil/IAR 的
post-build 命令行：

```powershell
powershell -File tools\board\build_schedule_map.ps1 `
  -BmelodRoot D:\proj\framework\Bmelod -ConfigFile D:\proj\bm_config_app.h `
  -Reg D:\proj\schedule_reg.c -Sources D:\proj\Source\control\xxx_schedule.c `
  -IncludeDirs D:\proj\Source -OutDir D:\proj\build\schedule_map
```

| 参数 | 必填 | 说明 |
|------|------|------|
| `-BmelodRoot` | 是 | 框架根目录 |
| `-ConfigFile` | 否 | 应用 `bm_config_app.h` 绝对路径，留空则用框架默认容量宏 |
| `-Reg` | 是 | 注册单元 `.c`（手写，满足 [bm_tt_schedule API §9.4](../05-API参考/bm_tt_schedule.md#94-注册单元契约bm_schedule_map_regh) 契约，或复制
  `tests/tools/schedule_map_fixture_reg.c` 改表名） |
| `-Sources` | 是 | 装配文件列表 |
| `-IncludeDirs` | 否 | 装配文件依赖的 include 目录 |
| `-OutDir` | 是 | 出表目录 |
| `-Generator` | 否 | 宿主子构建生成器，默认 `Ninja`（需要单一配置产物目录，避免 Visual Studio 之类多配置生成器把产物插进 `Debug/Release` 子目录导致路径不可预测） |
| `-CCompiler` | 否 | 宿主 C 编译器绝对路径，留空让 CMake 自动探测 `PATH` 上的默认编译器 |

脚本内部顺序：配置子工程 → 构建 → 跑 `schedule_map_dump <OutDir>` →
跑 `schedule_map_tool.py --dir <OutDir> --out <OutDir>/schedule_map_all.txt`，
任一步非零退出码都会让脚本以同样的退出码提前结束（`$LASTEXITCODE`）。

### 5.2 三档：真实 target 端 `report_json` 经 UART 导出

真实硬件上没有条件跑宿主子构建（交叉工具链、无法本地起 CMake 进程）
时，直接在固件里调用 `bm_tt_schedule_report_json`，把逐行 JSON 经串口
打印出来，PC 端用串口终端抓取、去掉多余的日志前缀后另存为 `.json`
文件，再喂给 `tools/schedule_map_tool.py`（用法与一档产出的 JSON
完全一致，工具不关心 JSON 是构建期宿主产出的还是真机 UART 抓下来的）：

```c
static void uart_emit_line(const char *line, void *u) {
    (void)u;
    uart_write_line(line); /* 逐行发送：一次 uart_write + 换行 */
}

void debug_dump_schedule_json(void) {
    bm_tt_schedule_json_meta_t meta = {
        .cpu = 0u, .ref_clk_hz = 168000000u,
        .operating_points_hz = NULL, .operating_point_count = 0u,
    };
    bm_tt_schedule_report_json(&sched_axis, &meta, uart_emit_line, NULL);
}
```

```bash
# PC 端：串口终端另存为 axis.json 后
python tools/schedule_map_tool.py axis.json --html axis.html
```

---

## 6. DVFS 立场

schedule-map 对"频率会变"这件事的立场很明确，避免被误用成一个假装
精确的实时估算工具：

1. **缺省锁频**：框架默认假设 CPU 跑在**固定频率**上，`wcet_us`
   本身就是在某个具体时钟频率下声明/实测出来的静态值，`report`/
   `report_json` 不做、也不能做任何频率相关的换算。
2. **工作点是 advisory（仅供参考）**：`ref_clk_hz`/`operating_points_hz`
   全部是调用方**主动提供**的元信息，门面本身不读任何时钟寄存器、不
   感知真实运行频率；级 2 工具用它们做的频率缩放估算永远标注
   `(estimated)`，是"如果换到这个频率大概会怎样"的参考数字，不是
   保证。
3. **实测回填升级 `OVERHEAD_CALIBRATED`**：`BM_CONFIG_TT_SCHED_OVERHEAD_US`
   缺省 `0`（未标定占位），一旦在真机上实测出框架派发开销的真实值，
   把它填进这个宏、同时把 `BM_CONFIG_TT_SCHED_OVERHEAD_CALIBRATED` 置
   `1`——这一步是本门面对"声明值 vs 实测值"边界的唯一升级路径，`report`/
   `report_json` 会如实反映 `overhead_calibrated=true`，下游工具据此
   知道这份负载账里的开销数字不再是占位。
4. **主频数据来自 config，不来自 port 运行期查询**：`ref_clk_hz`/
   `operating_points_hz` 这两个字段的数据源是应用层 `BM_CONFIG_CPU_FREQ_HZ`
   /`BM_CONFIG_CPU_DVFS_POINTS_HZ`（`include/bm_config.h`），不是
   `bm_hal_cpu_freq_*` 这套 port 层接口（[HAL 契约与移植要点
   §CPU 主频接口](../03-移植与IDE集成/01-HAL契约与移植要点.md#cpu-主频接口bm_hal_cpu_freq_)）
   的运行期查询结果——出表程序跑在宿主 PC（host 构建），链接的是 native
   port，读不到目标芯片（如 ESP32）的运行期真值，只能读 host 可见的静态
   config 声明；`bm_hal_cpu_freq_*` 面向的是目标板运行期（未来 PM）与开机
   自检，两条线各喂各的消费者，互不覆盖。声明了 `BM_CONFIG_CPU_DVFS_POINTS_HZ`
   （多档主频）时，`schedule_map_tool.py` 会为每个频率各出一张理论换算表
   （`OP @<Hz>: est 峰值 ... (estimated)`），再加一张跨频率的对比总表；只声
   明单一 `BM_CONFIG_CPU_FREQ_HZ` 时只出锚点频率下的这一张表。若应用声明了
   DVFS 点集却漏声明 `BM_CONFIG_CPU_FREQ_HZ` 锚点（`ref_clk_hz==0` 但
   `operating_points_hz` 非空），工具退化为只出单表，并给出明确告警"声明了
   DVFS 点但缺 `BM_CONFIG_CPU_FREQ_HZ` 锚点，无法频率缩放"（区别于两者都未
   声明时"参考时钟未声明"的泛化告警）——因为频率缩放公式
   `wcet(f) = ceil(wcet_ref × ref/f)` 必须有锚点 `ref` 才能外推。真正"运行
   期切频"（PM 依据负载/温度动态选档、并在切频后按新频率重新估算）不在
   本工具范围内，属于后续 PM 子系统的能力。

---

## 7. 相关文档

- API 参考（签名/字段/注册单元契约）：[../05-API参考/bm_tt_schedule](../05-API参考/bm_tt_schedule.md)
- CMake 接入函数参数表：[../02-构建与工具链/01-CMake选项与bm_config](../02-构建与工具链/01-CMake选项与bm_config.md#bm_add_schedule_map一行接入调度表导出)
- 混合域接线总览：[05-混合域接线](05-混合域接线.md)
