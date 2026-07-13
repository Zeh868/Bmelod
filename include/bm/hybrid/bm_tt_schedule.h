/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_tt_schedule.h
 * @brief 时间触发调度门面（bm_tt_schedule）：LET 任务表 + 静态绑定
 *
 * 开发者只写纯 step 函数与两张静态输入/输出绑定表；调度表由
 * `BM_SCHEDULE_DEFINE` 声明，单个 LET 任务由 `BM_LET_DEFINE_ISR`/
 * `BM_LET_DEFINE_MAINLOOP`（内部通用形式 `BM_LET_DEFINE_EX`）声明——宏隐藏
 * 快照区、双缓冲、per-input 运行态（miss/stale/age）等全部 bookkeeping。
 *
 * 本轮（接法 B）仅覆盖 kind=COMPUTE：ISR 域 step 与派发同步完成；
 * MAINLOOP 域 step 在派发时只冻结输入并置 pending，真正执行由
 * `bm_tt_schedule_run_pending` 在主循环中调用。
 *
 * @core_affinity 本核（per-CPU）
 * 调度表实例、rt 状态均为静态分配，跨核使用需各核独立实例。
 * @author zeh (china_qzh@163.com)
 * @version 1.5
 * @date 2026-07-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-01       1.0            zeh            骨架发布（config+公共头+CMake，无算法实现）
 * 2026-07-01       1.1            zeh            `BM_LET_DEFINE` 拆分为 `BM_LET_DEFINE_ISR`/
 *                                                 `BM_LET_DEFINE_MAINLOOP`（内部通用形式
 *                                                 `BM_LET_DEFINE_EX`），domain 由宏名显式区分
 * 2026-07-03       1.2            zeh            Task 2：新增 `bm_tt_schedule_report_json` 机器可读
 *                                                 JSON 导出（schema v1）+ meta 结构体
 * 2026-07-03       1.3            zeh            Task 7：在 `BM_LET_DEFINE_EX` 中新增编译期
 *                                                 `_Static_assert` 硬门（every >= 1 / at < every），
 *                                                 使非法相位组装在构建期而非运行期失败
 * 2026-07-04       1.4            zeh            Task 5：新增 `bm_tt_sched_intf_src_t` 干扰源类型，
 *                                                 `bm_tt_schedule_json_meta_t` 追加
 *                                                 interference/interference_count，供
 *                                                 report_json 导出 interference_sources
 * 2026-07-13       1.5            zeh            C3/C8：注明 input/output elem_size 须与所绑
 *                                                 bus 存储一致（init 运行期校验，与 safe_default
 *                                                 非空校验一并在 bm_tt_schedule_init 落地）
 *
 */
#ifndef BM_TT_SCHEDULE_H
#define BM_TT_SCHEDULE_H

#include "bm/hybrid/bm_hrt.h"
#include "bm/core/bm_bus.h"
#include "bm/common/bm_types.h"

/** 保质期哨兵：init 期解析为 BM_LET_AGE_DEFAULT_PERIODS×任务周期 */
#define BM_LET_AGE_DEFAULT 0xFFFFFFFFu

/** 默认保质期对应的任务周期倍数（max_age = N×period） */
#define BM_LET_AGE_DEFAULT_PERIODS 2u

/** 任务轴枚举：kind（本轮仅 compute） */
typedef enum {
    BM_TT_KIND_COMPUTE = 0
} bm_tt_kind_t;

/** 任务轴枚举：domain（执行域） */
typedef enum {
    BM_TT_DOMAIN_ISR = 0,
    BM_TT_DOMAIN_MAINLOOP = 1
} bm_tt_domain_t;

/** 输入绑定：源 LATEST bus + 保质期 */
typedef struct {
    const bm_bus_t *bus;          /**< 核内 LATEST 源 */
    uint32_t        max_age_us;   /**< 0=显式不检龄；BM_LET_AGE_DEFAULT=2×任务周期 */
    uint32_t        elem_size;    /**< 快照/拷出字节数（须与所绑 bus 存储 elem_size 一致，init 校验） */
    const void     *safe_default; /**< 冻结失败时填充（非 NULL，由 bm_tt_schedule_init 运行期校验） */
} bm_let_input_t;

/** 输出绑定：目标 LATEST bus + 安全值 */
typedef struct {
    bm_bus_t       *bus;
    uint32_t        elem_size;    /**< 须与目标 bus 存储 elem_size 一致，init 校验 */
    const void     *safe_default; /**< 非 NULL，由 bm_tt_schedule_init 运行期强制 */
} bm_let_output_t;

/** step 上下文（不透明，仅经访问器读写） */
typedef struct bm_let_ctx bm_let_ctx_t;

/**
 * @brief step 内读取输入快照
 *
 * @param ctx step 上下文
 * @param in_idx 输入索引（对应绑定表下标）
 * @param out_stale 输出：本次是否为 stale（安全值兜底）
 * @param out_age_us 输出：数据新鲜度（微秒）
 * @return 输入数据只读指针
 */
const void *bm_let_in(bm_let_ctx_t *ctx, uint32_t in_idx, int *out_stale,
                       uint32_t *out_age_us);

/**
 * @brief step 内获取输出写入缓冲
 *
 * @param ctx step 上下文
 * @param out_idx 输出索引（对应绑定表下标）
 * @return 输出数据写指针
 */
void *bm_let_out(bm_let_ctx_t *ctx, uint32_t out_idx);

/** 纯函数 step 签名 */
typedef void (*bm_let_step_fn)(bm_let_ctx_t *ctx, void *state);

/** 每任务运行态（BM_LET_DEFINE 分配，引擎读写；开发者不碰） */
typedef struct {
    uint8_t   phase;         /**< 双缓冲选择位，step 成功完成时翻转 */
    uint8_t   running;       /**< ISR 域 reentry guard */
    uint8_t   pending;       /**< MAINLOOP 域：已冻结、待主循环跑 step */
    uint8_t   fresh;         /**< MAINLOOP 域：有新完成结果、待下一 tick 发布 */
    uint32_t  overrun_count;
    uint32_t *baseline_seq;  /**< per-input，init 期快照 */
    uint32_t *miss;          /**< per-input miss 计数 */
    int      *stale;         /**< per-input，冻结时写、step 读（ISR/MAINLOOP 共用） */
    uint32_t *age_us;        /**< per-input */
} bm_let_task_rt_t;

/** 一个 activity = 调度表里一行 */
typedef struct {
    const char             *name;
    uint16_t                every;       /**< 任务周期 = minor_us × every */
    uint16_t                at;          /**< 相位，at < every */
    bm_tt_kind_t            kind;
    bm_tt_domain_t          domain;      /**< 执行域，由 BM_LET_DEFINE_ISR/_MAINLOOP 宏名显式设定 */
    uint32_t                wcet_us;
    bm_let_step_fn          step;
    void                   *state;
    const bm_let_input_t   *inputs;
    uint8_t                 input_count;
    const bm_let_output_t  *outputs;
    uint8_t                 output_count;
    /* 宏分配的存储 */
    void                   *snapshot;    /**< 连续快照区，Σ input.elem_size */
    void                   *outbuf;      /**< 连续双缓冲，2 × Σ output.elem_size */
    bm_let_task_rt_t       *rt;
} bm_tt_activity_t;

/** 调度表 */
typedef struct {
    const char        *name;
    uint32_t           minor_us;
    bm_tt_activity_t **entries;      /**< 指针表，见 BM_SCHEDULE_DEFINE */
    uint8_t            entry_count;
    /* 运行态 */
    uint32_t           tick_idx;     /**< 0..N-1，到 N 归零 */
    uint32_t           n_frames;     /**< = LCM(every)，init 期算 */
} bm_tt_schedule_t;

/** RTA 中立只读描述符（喂 mp 胶水，门面不 include mp 头） */
typedef struct {
    uint8_t  owner_cpu;
    uint8_t  kind;
    uint8_t  domain;
    uint32_t wcet_us;
    uint32_t period_us;
    uint32_t deadline_us;
} bm_tt_schedule_rt_slot_t;

/**
 * @brief 初始化调度表（校验 every/at、绑定表、分配布局）
 *
 * @param sched 调度表实例
 * @return BM_OK 成功；其他为错误码
 */
int bm_tt_schedule_init(bm_tt_schedule_t *sched);

/**
 * @brief 生成本调度表对应的 HRT slot 描述（period_us=minor_us，callback=派发器）
 *
 * @param sched 调度表实例
 * @return HRT slot 描述符
 */
bm_hrt_slot_t bm_tt_schedule_hrt_slot(bm_tt_schedule_t *sched);

/**
 * @brief ISR 派发器：hrt ISR 回调转此
 *
 * ISR 域 step 同步跑；MAINLOOP 域只冻结输入并置 pending，交由
 * bm_tt_schedule_run_pending 在主循环中执行。
 *
 * @param sched 调度表实例
 */
void bm_tt_schedule_tick(bm_tt_schedule_t *sched);

/**
 * @brief 主循环调用：跑 MAINLOOP 域 pending step
 *
 * @param sched 调度表实例
 * @param budget 本次最多运行的任务数
 * @return 本次实际运行的任务数
 */
uint32_t bm_tt_schedule_run_pending(bm_tt_schedule_t *sched, uint32_t budget);

/**
 * @brief 输出调度表可读诊断报告（逐行经 emit 回调发出）
 *
 * @param sched 调度表实例（只读）
 * @param emit 逐行输出回调
 * @param u emit 回调透传上下文
 */
void bm_tt_schedule_report(const bm_tt_schedule_t *sched,
                           void (*emit)(const char *line, void *u), void *u);

/**
 * @brief 干扰源描述（HRT 抢占干扰来源，供 schedule-map 消费方合并分析）
 *
 * @details 一个干扰源代表调度表之外、会抢占/挤占本表时间格的一路周期性
 * 活动：`tier` 区分其来源层级——0（hardware）为硬件中断（不受本调度表
 * 节拍约束，可能任意相位抢占）、1（scheduled）为已被纳入某调度表节拍的
 * 活动（相位/周期已知，可做更精确的 RTA 合并）。本类型仅描述、不参与
 * 框架运行期调度，由 app/注册单元在装配 `bm_tt_schedule_json_meta_t` 时
 * 提供，`bm_tt_schedule_report_json` 只读遍历并导出。
 */
typedef struct {
    const char *name;      /**< 干扰源标识（建议合法 C 标识符，导出时不转义） */
    uint32_t    period_us; /**< 周期（微秒） */
    uint32_t    wcet_us;   /**< 最坏执行时间/占用时长（微秒） */
    uint8_t     tier;      /**< 来源层级：0=hardware（硬件中断），1=scheduled（已调度） */
} bm_tt_sched_intf_src_t;

/**
 * @brief JSON 导出元数据（由 app/注册单元提供；NULL 视为全零默认值）
 */
typedef struct {
    uint8_t         cpu;                    /**< 该表所属 CPU；单核填 0 */
    uint32_t        ref_clk_hz;             /**< 声明 wcet 所基于的参考时钟；0=未声明 */
    const uint32_t *operating_points_hz;    /**< 工作点频率数组，可为 NULL */
    uint8_t         operating_point_count;  /**< 工作点个数 */
    const bm_tt_sched_intf_src_t *interference;      /**< 干扰源数组，可为 NULL */
    uint8_t                       interference_count; /**< 干扰源个数 */
} bm_tt_schedule_json_meta_t;

/**
 * @brief 输出调度表的机器可读 JSON 报告（schema v1）
 *
 * @details 经 @p emit 逐行输出一个 JSON 对象，涵盖：调度表身份信息
 * （name/minor_us/n_frames/hyperperiod_us）、框架开销
 * （`BM_CONFIG_TT_SCHED_OVERHEAD_US` / `BM_CONFIG_TT_SCHED_OVERHEAD_CALIBRATED`）、
 * 调用方传入的 meta（cpu/ref_clk_hz/operating_points_hz）、一个 `tasks` 数组
 * （每个 activity 一条，按 `entries` 顺序；`name` 取自声明宏的 `#id` 字符串化，
 * 恒为合法 C 标识符，因此无需 JSON 转义）、一个 `frames` 数组（每个 minor
 * frame 一条，t 升序排列，`isr_load_us` 含 `BM_CONFIG_TT_SCHED_OVERHEAD_US`，
 * `mainloop_pending_us` 为该拍命中的 MAINLOOP 域 activity 的 wcet_us 之和），
 * 一个 `interference_sources` 数组（由 meta 的 `interference`/
 * `interference_count` 提供，逐源一行，`tier` 导出为 "hardware"/
 * "scheduled" 字符串；`interference_count` 为 0 或 meta 为 NULL 时导出空
 * 数组 `[]`），以及一个预留的空 `edges` 数组（由后续任务填充）。输出是
 * 确定性的：相同的调度表状态恒产出逐字节相同的输出。
 *
 * @param sched 调度表实例（只读）
 * @param meta 导出元数据；NULL 时退化为全零默认值
 * @param emit 逐行输出回调
 * @param u emit 回调透传上下文
 */
void bm_tt_schedule_report_json(const bm_tt_schedule_t *sched,
                                const bm_tt_schedule_json_meta_t *meta,
                                void (*emit)(const char *line, void *u), void *u);

/**
 * @brief 查询调度表可导出的 RTA slot 数量
 *
 * @param sched 调度表实例（只读）
 * @return slot 数量
 */
uint32_t bm_tt_schedule_rt_slot_count(const bm_tt_schedule_t *sched);

/**
 * @brief 按索引导出 RTA 中立只读描述符
 *
 * @param sched 调度表实例（只读）
 * @param idx slot 索引
 * @param out 输出描述符
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效或索引越界
 */
int bm_tt_schedule_rt_slot_at(const bm_tt_schedule_t *sched, uint32_t idx,
                              bm_tt_schedule_rt_slot_t *out);

/**
 * @brief 内部通用形式：一行声明 LET 任务，宏分配 snapshot/双缓冲/rt 全部
 * bookkeeping，domain 由调用者显式传入
 *
 * 业务代码请优先用具名形式 `BM_LET_DEFINE_ISR`/`BM_LET_DEFINE_MAINLOOP`——
 * 二者都是本宏的薄包装，仅把 domain 参数固化成字面量，语义与本宏逐字相同。
 * 本宏保留给需要按变量/宏参数动态决定 domain 的极少数场景（如代码生成）。
 *
 * @param id       任务实例名（不带引号）。同时用作 activity 变量名（传给
 *                 BM_SCHEDULE_DEFINE 时写 &id）与内部静态存储前缀。
 * @param domain_  执行域（bm_tt_domain_t）：BM_TT_DOMAIN_ISR/BM_TT_DOMAIN_MAINLOOP。
 * @param every_   分频：任务周期 = minor_us × every（每 every 个 minor 拍跑一次）。
 * @param at_      相位/错峰：从超周期内第 at 拍起算（须 0 ≤ at < every），
 *                 用于把同频任务岔开到不同 minor 格。
 * @param wcet_    最坏执行时间（µs）。喂节拍负载校验（Σ本格 wcet ≤ minor_us）
 *                 与调度概览报告；须为实测或保守静态分析值。
 * @param step_    纯函数 step 回调，签名 void(bm_let_ctx_t*, void* state)。只准
 *                 读冻结输入(bm_let_in)、写输出(bm_let_out)、读写自己的 state；
 *                 禁止在 step 内发布 bus/读时钟/阻塞/调度（LET 确定性前提）。
 * @param state_   step 的自持状态指针（透传给 step 第二参，可为 NULL）。
 * @param inputs_  const bm_let_input_t[] 输入绑定表（bus/max_age_us/elem_size/
 *                 safe_default）。数量由 sizeof 自动推导。
 * @param outputs_ const bm_let_output_t[] 输出绑定表（bus/elem_size/safe_default，
 *                 safe_default 非空由 init 强制）。数量由 sizeof 自动推导。
 *
 * @note 实现细节：连续输出双缓冲按最大元素上界 BM_CONFIG_TT_SCHED_MAX_ELEM_SIZE
 *       × 输出数分配（略有余量、换零动态分配与实现简单）。
 * @note 编译期硬门：`every_`、`at_` 须为满足 `every >= 1` 且 `at < every` 的
 *       常量表达式；违反者经 `_Static_assert` 在构建期直接失败，而非留到
 *       运行期才被（或未被）捕获。
 */
#define BM_LET_DEFINE_EX(id, domain_, every_, at_, wcet_, step_, state_, inputs_, outputs_)     \
    _Static_assert((every_) >= 1u, "BM_LET_DEFINE: every must be >= 1");                         \
    _Static_assert((at_) < (every_), "BM_LET_DEFINE: at must be < every");                       \
    static uint8_t  id##_snap[BM_CONFIG_TT_SCHED_MAX_ELEM_SIZE *                                \
                              (sizeof(inputs_) / sizeof((inputs_)[0]))];                        \
    static uint8_t  id##_out2[2u * BM_CONFIG_TT_SCHED_MAX_ELEM_SIZE *                           \
                              (sizeof(outputs_) / sizeof((outputs_)[0]))];                      \
    static uint32_t id##_baseseq[(sizeof(inputs_) / sizeof((inputs_)[0]))];                     \
    static uint32_t id##_miss[(sizeof(inputs_) / sizeof((inputs_)[0]))];                        \
    static int      id##_stale[(sizeof(inputs_) / sizeof((inputs_)[0]))];                       \
    static uint32_t id##_age[(sizeof(inputs_) / sizeof((inputs_)[0]))];                         \
    static bm_let_task_rt_t id##_rt = { 0u, 0u, 0u, 0u, 0u,                                     \
        id##_baseseq, id##_miss, id##_stale, id##_age };                                        \
    bm_tt_activity_t id = {                                                                     \
        .name = #id, .every = (every_), .at = (at_),                                            \
        .kind = BM_TT_KIND_COMPUTE, .domain = (domain_), .wcet_us = (wcet_),                    \
        .step = (step_), .state = (state_),                                                     \
        .inputs = (inputs_),  .input_count  = (uint8_t)(sizeof(inputs_)/sizeof((inputs_)[0])),  \
        .outputs = (outputs_),.output_count = (uint8_t)(sizeof(outputs_)/sizeof((outputs_)[0])),\
        .snapshot = id##_snap, .outbuf = id##_out2, .rt = &id##_rt                              \
    }

/**
 * @brief 一行声明 **ISR 域** LET 任务：step 在 hrt ISR 内同步跑完
 *
 * ISR 域任务对"短"是硬要求——`step` 必须简短、确定性强，`wcet_us` 之和要能
 * 在 `minor_us` 内跑完（`bm_tt_schedule_init` 会按此做节拍负载校验）；计算量大
 * 或耗时不确定的重任务请改用 `BM_LET_DEFINE_MAINLOOP`。
 *
 * @param id       任务实例名（不带引号）。同时用作 activity 变量名（传给
 *                 BM_SCHEDULE_DEFINE 时写 &id）与内部静态存储前缀。
 * @param every_   分频：任务周期 = minor_us × every（每 every 个 minor 拍跑一次）。
 * @param at_      相位/错峰：从超周期内第 at 拍起算（须 0 ≤ at < every），
 *                 用于把同频任务岔开到不同 minor 格。
 * @param wcet_    最坏执行时间（µs）。喂节拍负载校验（Σ本格 wcet ≤ minor_us）
 *                 与调度概览报告；须为实测或保守静态分析值。
 * @param step_    纯函数 step 回调，签名 void(bm_let_ctx_t*, void* state)。只准
 *                 读冻结输入(bm_let_in)、写输出(bm_let_out)、读写自己的 state；
 *                 禁止在 step 内发布 bus/读时钟/阻塞/调度（LET 确定性前提）。
 * @param state_   step 的自持状态指针（透传给 step 第二参，可为 NULL）。
 * @param inputs_  const bm_let_input_t[] 输入绑定表（bus/max_age_us/elem_size/
 *                 safe_default）。数量由 sizeof 自动推导。
 * @param outputs_ const bm_let_output_t[] 输出绑定表（bus/elem_size/safe_default，
 *                 safe_default 非空由 init 强制）。数量由 sizeof 自动推导。
 */
#define BM_LET_DEFINE_ISR(id, every_, at_, wcet_, step_, state_, inputs_, outputs_)             \
    BM_LET_DEFINE_EX(id, BM_TT_DOMAIN_ISR, every_, at_, wcet_, step_, state_, inputs_, outputs_)

/**
 * @brief 一行声明 **MAINLOOP 域** LET 任务：ISR 只冻结挂起，step 由主循环
 * `bm_tt_schedule_run_pending` 执行
 *
 * 适合重计算/耗时不确定的任务（滤波器整定、诊断统计、日志格式化等）：
 * ISR 内只做输入冻结（快、确定性强），真正的 `step` 延后到主循环里按预算跑，
 * 换来的代价是结果多晚一拍发布（LET +1 拍语义）；业务侧需保证主循环**周期性
 * 调用** `bm_tt_schedule_run_pending(sched, budget)`，否则会记 overrun 并丢本拍。
 *
 * @param id       任务实例名（不带引号）。同时用作 activity 变量名（传给
 *                 BM_SCHEDULE_DEFINE 时写 &id）与内部静态存储前缀。
 * @param every_   分频：任务周期 = minor_us × every（每 every 个 minor 拍跑一次）。
 * @param at_      相位/错峰：从超周期内第 at 拍起算（须 0 ≤ at < every），
 *                 用于把同频任务岔开到不同 minor 格。
 * @param wcet_    最坏执行时间（µs）。喂调度概览报告的预算账；须为实测或
 *                 保守静态分析值。
 * @param step_    纯函数 step 回调，签名 void(bm_let_ctx_t*, void* state)。只准
 *                 读冻结输入(bm_let_in)、写输出(bm_let_out)、读写自己的 state；
 *                 禁止在 step 内发布 bus/读时钟/阻塞/调度（LET 确定性前提）。
 * @param state_   step 的自持状态指针（透传给 step 第二参，可为 NULL）。
 * @param inputs_  const bm_let_input_t[] 输入绑定表（bus/max_age_us/elem_size/
 *                 safe_default）。数量由 sizeof 自动推导。
 * @param outputs_ const bm_let_output_t[] 输出绑定表（bus/elem_size/safe_default，
 *                 safe_default 非空由 init 强制）。数量由 sizeof 自动推导。
 */
#define BM_LET_DEFINE_MAINLOOP(id, every_, at_, wcet_, step_, state_, inputs_, outputs_)        \
    BM_LET_DEFINE_EX(id, BM_TT_DOMAIN_MAINLOOP, every_, at_, wcet_, step_, state_, inputs_, outputs_)

/**
 * @brief 一行声明调度表：宏生成 activity 指针表
 *
 * @param id        调度表实例名（不带引号）。传给 bm_tt_schedule_init/_hrt_slot。
 * @param minor_us_ 基本节拍（µs）= hrt 心跳周期 = 各任务周期的 GCD；时间轴最小刻度。
 * @param ...       该表包含的 activity 地址列表（各 BM_LET_DEFINE 实例取 &，
 *                  如 &balance, &estimator, &telemetry）。数量由 sizeof 自动推导。
 *
 * @note entries 为指针表（bm_tt_activity_t**），每 activity 是 BM_LET_DEFINE
 *       生成的独立静态实例，可跨表复用、rt 状态天然每实例独立。
 */
#define BM_SCHEDULE_DEFINE(id, minor_us_, ...)                                                  \
    static bm_tt_activity_t *id##_entries_ptr[] = { __VA_ARGS__ };                              \
    bm_tt_schedule_t id = {                                                                     \
        .name = #id, .minor_us = (minor_us_),                                                   \
        .entries = id##_entries_ptr,   /* 指针表：每 activity 独立静态实例 */                   \
        .entry_count = (uint8_t)(sizeof(id##_entries_ptr)/sizeof(id##_entries_ptr[0]))          \
    }

#endif /* BM_TT_SCHEDULE_H */
