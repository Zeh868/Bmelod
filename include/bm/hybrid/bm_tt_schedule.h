/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_tt_schedule.h
 * @brief 时间触发调度门面（bm_tt_schedule）：LET 任务表 + 静态绑定
 *
 * 开发者只写纯 step 函数与两张静态输入/输出绑定表；调度表由
 * `BM_SCHEDULE_DEFINE` 声明，单个 LET 任务由 `BM_LET_DEFINE` 声明——宏隐藏
 * 快照区、双缓冲、per-input 运行态（miss/stale/age）等全部 bookkeeping。
 *
 * 本轮（接法 B）仅覆盖 kind=COMPUTE：ISR 域 step 与派发同步完成；
 * MAINLOOP 域 step 在派发时只冻结输入并置 pending，真正执行由
 * `bm_tt_schedule_run_pending` 在主循环中调用。
 *
 * @core_affinity 本核（per-CPU）
 * 调度表实例、rt 状态均为静态分配，跨核使用需各核独立实例。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-01       1.0            zeh            骨架发布（config+公共头+CMake，无算法实现）
 *
 */
#ifndef BM_TT_SCHEDULE_H
#define BM_TT_SCHEDULE_H

#include "bm/hybrid/bm_hrt.h"
#include "bm/core/bm_bus.h"
#include "bm/common/bm_types.h"

/** 保质期哨兵：init 期解析为 2×任务周期 */
#define BM_LET_AGE_DEFAULT 0xFFFFFFFFu

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
    uint32_t        elem_size;    /**< 快照/拷出字节数 */
    const void     *safe_default; /**< 冻结失败时填充（非 NULL，编译期由宏强制） */
} bm_let_input_t;

/** 输出绑定：目标 LATEST bus + 安全值 */
typedef struct {
    bm_bus_t       *bus;
    uint32_t        elem_size;
    const void     *safe_default; /**< 非 NULL，编译期强制 */
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
    bm_tt_domain_t          domain;      /**< 缺省 ISR */
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
 * @brief 一行声明 LET 任务：宏分配 snapshot/双缓冲/rt 全部 bookkeeping
 *
 * 计算连续输出双缓冲字节数：调用者用绑定表已给 elem_size，
 * 这里用最大元素上界（BM_CONFIG_TT_SCHED_MAX_ELEM_SIZE）简化对齐。
 */
#define BM_LET_DEFINE(id, every_, at_, wcet_, step_, state_, inputs_, outputs_)                 \
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
        .kind = BM_TT_KIND_COMPUTE, .domain = BM_TT_DOMAIN_ISR, .wcet_us = (wcet_),             \
        .step = (step_), .state = (state_),                                                     \
        .inputs = (inputs_),  .input_count  = (uint8_t)(sizeof(inputs_)/sizeof((inputs_)[0])),  \
        .outputs = (outputs_),.output_count = (uint8_t)(sizeof(outputs_)/sizeof((outputs_)[0])),\
        .snapshot = id##_snap, .outbuf = id##_out2, .rt = &id##_rt                              \
    }

/**
 * @brief 一行声明调度表：宏生成 activity 指针表
 */
#define BM_SCHEDULE_DEFINE(id, minor_us_, ...)                                                  \
    static bm_tt_activity_t *id##_entries_ptr[] = { __VA_ARGS__ };                              \
    bm_tt_schedule_t id = {                                                                     \
        .name = #id, .minor_us = (minor_us_),                                                   \
        .entries = id##_entries_ptr,   /* 指针表：每 activity 独立静态实例 */                   \
        .entry_count = (uint8_t)(sizeof(id##_entries_ptr)/sizeof(id##_entries_ptr[0]))          \
    }

#endif /* BM_TT_SCHEDULE_H */
