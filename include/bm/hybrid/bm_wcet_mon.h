/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_wcet_mon.h
 * @brief 运行时 deadline/WCET 监控（SAFE-2）：通用监控段原语
 *
 * 一个 span = 一段被监控代码的静态实例：begin/end 计时（bm_uptime 墙钟）、
 * WCET 预算登记、超额检测；双层上报 = 模块级 sink 快回调（可在 ISR 上下文）
 * + 观测面计数拉取。BUDGET_OVERRUN=跑了但超时；DEADLINE_MISS=该跑没跑成
 * （由宿主经 bm_wcet_mon_report_miss 显式上报）。
 *
 * 并发契约：每 span 单写者（归属其执行上下文）；register/set_sink 仅限
 * init 阶段（运行前单线程窗口）；观测面 32 位对齐字段单核 ISR 写/主循环
 * 读无撕裂。设计全文见 docs/superpowers/specs/2026-07-02-safe2-wcet-mon-design.md。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-02
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-02       1.0            zeh            骨架发布（config+公共头+CMake+注册/迭代，无计时实现）
 *
 */
#ifndef BM_WCET_MON_H
#define BM_WCET_MON_H

#include <stdint.h>

/** 监控段：静态实例，app/宿主自有存储（零动态分配） */
typedef struct bm_wcet_span {
    /* 声明面（注册前由使用者填） */
    const char *name;          /**< 诊断名 */
    uint32_t    budget_us;     /**< WCET 预算；0 = 只计时不判超 */
    /* 内部状态（引擎读写，使用者不碰） */
    uint64_t    t0_us;         /**< begin 时刻（bm_uptime_us） */
    uint8_t     running;       /**< begin/end 配对状态 */
    /* 观测面（运行时累积，只读消费；计数饱和不回绕） */
    uint32_t    last_us;       /**< 最近一次实测 */
    uint32_t    max_us;        /**< 实测 WCET，单调增 */
    uint32_t    run_count;     /**< 完成的 begin/end 对数 */
    uint32_t    overrun_count; /**< 实测 > 预算 次数 */
    uint32_t    miss_count;    /**< 外部上报 deadline miss 次数 */
    uint32_t    misuse_count;  /**< begin/end 配对错误次数 */
} bm_wcet_span_t;

/** 一行声明监控段（与手写结构体初始化等价） */
#define BM_WCET_SPAN_DEFINE(id, budget_us_) \
    bm_wcet_span_t id = { .name = #id, .budget_us = (budget_us_) }

/** 事件种类：预算超额（跑了但超时）/ deadline miss（该跑没跑成） */
typedef enum {
    BM_WCET_EVT_BUDGET_OVERRUN = 0,
    BM_WCET_EVT_DEADLINE_MISS  = 1
} bm_wcet_evt_t;

/** sink 快回调：短、非阻塞、ISR-safe、可并发重入；miss 时 measured_us=0 */
typedef void (*bm_wcet_sink_fn)(const bm_wcet_span_t *span, bm_wcet_evt_t evt,
                                uint32_t measured_us, void *user);

/**
 * @brief 初始化 bm_wcet_mon 模块
 *
 * 清空注册表与 sink 绑定，须在任何 register/begin/end 调用之前于 init
 * 阶段（运行前单线程窗口）调用一次；重复调用等价于重置（用于测试隔离）。
 *
 * @return 无
 */
void bm_wcet_mon_init(void);

/**
 * @brief 注册一个监控段
 *
 * 仅限 init 阶段调用。span 存储归调用者所有（静态实例，零动态分配），
 * 本函数只记录指针；同一 span 指针重复注册视为错误。
 *
 * @param span 待注册的监控段实例指针，非 NULL
 * @return BM_OK 成功；BM_ERR_INVALID span 为 NULL；
 *         BM_ERR_ALREADY span 已注册过；BM_ERR_NO_MEM 注册表已满
 *         （见 BM_CONFIG_WCET_MON_MAX_SPANS）
 */
int  bm_wcet_mon_register(bm_wcet_span_t *span);

/**
 * @brief 设置模块级 sink 快回调
 *
 * 仅限 init 阶段调用（fn/user 非原子对，运行期变更不保证可见性一致）。
 * 传 NULL 关闭 sink 上报，此时仅观测面计数仍会累积。
 *
 * @param fn sink 回调函数指针，可为 NULL（关闭上报）
 * @param user 透传给 sink 的用户上下文指针，可为 NULL
 * @return 无
 */
void bm_wcet_mon_set_sink(bm_wcet_sink_fn fn, void *user);

/**
 * @brief 标记一个监控段开始计时
 *
 * 记录当前 bm_uptime_us() 为起始时刻；与 bm_wcet_mon_end 配对使用。
 * 重复 begin（未 end 即再次 begin）计入 misuse_count。
 *
 * @param span 目标监控段指针
 * @return 无
 */
void bm_wcet_mon_begin(bm_wcet_span_t *span);

/**
 * @brief 标记一个监控段结束计时
 *
 * 用当前 bm_uptime_us() 与 begin 记录的起始时刻计算实测耗时，更新
 * last_us/max_us/run_count；超过 budget_us（非 0）时计入 overrun_count
 * 并经 sink 上报 BM_WCET_EVT_BUDGET_OVERRUN。未配对 begin 即调用 end
 * 计入 misuse_count。
 *
 * @param span 目标监控段指针
 * @return 无
 */
void bm_wcet_mon_end(bm_wcet_span_t *span);

/**
 * @brief 显式上报一次 deadline miss（该跑没跑成）
 *
 * 用于宿主检测到某监控段本该运行但未被调度/未及时触发的场景；累加
 * miss_count 并经 sink 上报 BM_WCET_EVT_DEADLINE_MISS（measured_us=0）。
 *
 * @param span 目标监控段指针
 * @return 无
 */
void bm_wcet_mon_report_miss(bm_wcet_span_t *span);

/**
 * @brief 查询已注册监控段数量
 *
 * @return 当前已成功注册的监控段个数
 */
uint32_t bm_wcet_mon_span_count(void);

/**
 * @brief 按注册顺序索引访问监控段
 *
 * @param idx 索引，取值范围 [0, bm_wcet_mon_span_count())
 * @return 对应监控段的只读指针；idx 越界返回 NULL
 */
const bm_wcet_span_t *bm_wcet_mon_span_at(uint32_t idx);

#endif /* BM_WCET_MON_H */
