/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_module.h
 * @brief 模块生命周期管理
 *
 * 通过 bm_module_t 描述模块的 init/start/stop/deinit 回调，
 * 由框架按优先级统一调度。看门狗由应用 main 主循环统一喂，与模块无关。
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-06-12
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-12       1.2            zeh            移除模块与看门狗耦合
 * 2026-06-14       1.3            zeh            init_on_this_cpu 按 owner 过滤
 *
 */
#ifndef BM_MODULE_H
#define BM_MODULE_H

#include "bm/common/bm_types.h"
#include "bm/core/bm_event.h"
#include "bm/core/bm_cpu_local.h"

#ifndef BM_CONFIG_MAX_MODULES
#define BM_CONFIG_MAX_MODULES 8
#endif

/** 模块运行状态 */
typedef enum {
    BM_MODULE_STATE_UNINIT = 0,
    BM_MODULE_STATE_INITED,
    BM_MODULE_STATE_STARTED,
    BM_MODULE_STATE_STOPPED
} bm_module_state_t;

/** 模块所属运行时域 */
typedef enum {
    BM_DOMAIN_RT = 0,
    BM_DOMAIN_SRT,
    BM_DOMAIN_WORKER,
    BM_DOMAIN_COMMON
} bm_domain_t;

/** 模块描述符（编译期静态注册） */
typedef struct {
    const char              *name;
    uint8_t                  priority;
    bm_domain_t              domain;
    uint8_t                  owner_cpu;
    const bm_event_type_t   *subscribed_events;
    uint8_t                  subscribed_event_count;
    bm_module_state_t        state;
    int (*init)(void);
    int (*start)(void);
    int (*stop)(void);
    int (*deinit)(void);
} bm_module_t;

typedef uint8_t (*bm_module_owner_resolver_t)(uint32_t module_index);

/**
 * 单模块描述符（每模块一个 .c）。未使用的回调传 NULL，按成功的空操作处理。
 *
 * @code
 * BM_MODULE_DEFINE(sensor, 2,
 *     sensor_init, sensor_start, sensor_stop, sensor_deinit);
 * @endcode
 */
#define BM_MODULE_DEFINE(name, priority, init_fn, start_fn, stop_fn, deinit_fn) \
    const bm_module_t _bm_mod_##name = { \
        #name, \
        (uint8_t)(priority), \
        BM_DOMAIN_COMMON, \
        BM_CPU_ANY, \
        NULL, \
        0u, \
        BM_MODULE_STATE_UNINIT, \
        (init_fn), \
        (start_fn), \
        (stop_fn), \
        (deinit_fn) \
    }

/** 按 CPU 路由：预指定模块 owner 核与 `subscribed_events[]`（分区 build 前闭包校验）。 */
#define BM_MODULE_DEFINE_OWNER(name, priority, owner_cpu, event_list, \
                               init_fn, start_fn, stop_fn, deinit_fn) \
    static const bm_event_type_t _bm_mod_##name##_events[] = event_list; \
    const bm_module_t _bm_mod_##name = { \
        #name, \
        (uint8_t)(priority), \
        BM_DOMAIN_COMMON, \
        (uint8_t)(owner_cpu), \
        _bm_mod_##name##_events, \
        (uint8_t)(sizeof(_bm_mod_##name##_events) / \
                  sizeof(_bm_mod_##name##_events[0])), \
        BM_MODULE_STATE_UNINIT, \
        (init_fn), \
        (start_fn), \
        (stop_fn), \
        (deinit_fn) \
    }

#define BM_MODULE_DEFINE_DOMAIN(name, priority, domain, init_fn, start_fn, stop_fn, deinit_fn) \
    const bm_module_t _bm_mod_##name = { \
        #name, \
        (uint8_t)(priority), \
        (domain), \
        BM_CPU_ANY, \
        NULL, \
        0u, \
        BM_MODULE_STATE_UNINIT, \
        (init_fn), \
        (start_fn), \
        (stop_fn), \
        (deinit_fn) \
    }

/** 在 module_table.c 中前置声明各模块条目 */
#define BM_MODULE_DECLARE(name) \
    extern const bm_module_t _bm_mod_##name

/** 聚合表中的单条引用（指针，兼容 MSVC 静态初始化） */
#define BM_MODULE_ENTRY(name) \
    (&_bm_mod_##name)

/**
 * 应用侧模块表（通常单独 module_table.c）。
 */
#define BM_MODULE_TABLE(...) \
    const bm_module_t *_bm_module_table[] = { __VA_ARGS__ }; \
    const uint32_t _bm_module_count = \
        (uint32_t)(sizeof(_bm_module_table) / sizeof(_bm_module_table[0]))

/**
 * @brief 应用启动：event_reset → init_all → start_all
 */
int bm_module_boot(void);

/**
 * @brief 按优先级初始化所有已注册模块
 */
int bm_module_init_all(void);

/**
 * @brief 按优先级启动所有已初始化模块
 *
 * 已通过 bm_module_stop_all() 停止的模块也可再次启动。
 */
int bm_module_start_all(void);

/**
 * @brief 按优先级停止所有已启动模块
 */
int bm_module_stop_all(void);

/**
 * @brief 按优先级反初始化所有模块
 *
 * 若模块仍在运行，会先调用 stop；stop 失败的模块不会执行 deinit。
 */
int bm_module_deinit_all(void);

/**
 * @brief 本核模块 init（按分区表 owner_cpu 过滤）
 */
int bm_module_init_on_this_cpu(void);

/**
 * @brief 本核模块 start
 */
int bm_module_start_on_this_cpu(void);

/**
 * @brief Install the static partition owner resolver.
 *
 * Intended for the bootstrap path.
 */
void bm_module_set_owner_resolver(bm_module_owner_resolver_t resolver);

/**
 * @brief 注册模块表冻结时的附加钩子（NULL 清除）。
 *
 * bm_module_init_all / per-CPU init 完成后、冻结订阅表的同时调用。
 * 默认未设时无附加动作。用于让上层在模块冻结点附挂自有静态注册表冻结。
 */
void bm_module_set_freeze_hook(void (*hook)(void));

/**
 * @brief 获取当前应用静态模块表
 *
 * 返回值由 `BM_MODULE_TABLE(...)` 宏提供的只读数组承载；当应用未定义
 * 模块表时，返回 NULL。
 *
 * @return 模块表首地址，或 NULL
 */
const bm_module_t *const *bm_module_table(void);

/**
 * @brief 获取当前应用静态模块表元素个数
 *
 * @return 模块条目数；未定义模块表时返回 0
 */
uint32_t bm_module_count(void);

#endif /* BM_MODULE_H */
