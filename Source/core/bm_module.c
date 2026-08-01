/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_module.c
 * @brief 模块生命周期管理实现
 *
 * 从应用提供的 _bm_module_table 加载模块，按优先级排序后依次 init/start/stop/deinit。
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-02
 *
 * @par 修改日志:
 * 2026-08-01       1.2            Codex           补齐 Doxygen 合规元数据
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-10       1.1            zeh            失败回滚与状态机加固
 * 2026-07-02       1.2            zeh            QD-6：cache-line 补齐改用 union，
 *                                                消除 MSVC C2233
 *
 */
#include "bm_module.h"
#include "bm/core/bm_module_domain.h"
#include "bm_critical_wrap.h"
#include "bm_event.h"
#include "bm_log.h"
#include "bm/core/bm_cpu_local.h"

#include <stdbool.h>
#include <string.h>

extern const bm_module_t *_bm_module_table[];
extern const uint32_t     _bm_module_count;

enum {
    BM_MODULES_UNINITIALIZED = 0,
    BM_MODULES_INITIALIZING,
    BM_MODULES_READY,
    BM_MODULES_TRANSITIONING,
    BM_MODULES_CLEANUP_PENDING
};

typedef struct {
    bm_module_t modules[BM_CONFIG_MAX_MODULES];
    uint32_t module_count;
    int initialized;
} bm_module_cpu_state_t;

typedef BM_CACHE_LINE_PADDED_UNION(bm_module_cpu_state_t, state,
                                   BM_CONFIG_CACHE_LINE) bm_module_cpu_storage_t;

static BM_CACHE_ALIGNAS(BM_CONFIG_CACHE_LINE)
bm_module_cpu_storage_t g_module_cpu[BM_CONFIG_CPU_COUNT];
static bm_module_owner_resolver_t s_owner_resolver;

/**
 * @brief 获取当前 CPU 的模块运行时状态
 *
 * @return 状态指针；CPU 越界时返回 NULL
 */
static bm_module_cpu_state_t *bm_module_this(void) {
    uint32_t cpu = BM_CPU_THIS();

    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return NULL;
    }
    return &g_module_cpu[cpu].state;
}

/**
 * @brief 设置模块归属解析器
 *
 * @param resolver 解析器函数指针
 */
void bm_module_set_owner_resolver(bm_module_owner_resolver_t resolver) {
    s_owner_resolver = resolver;
}

static void (*s_freeze_hook)(void);

void bm_module_set_freeze_hook(void (*hook)(void)) {
    s_freeze_hook = hook;
}

const bm_module_t *const *bm_module_table(void) {
    return _bm_module_table;
}

uint32_t bm_module_count(void) {
    return _bm_module_count;
}

/**
 * @brief 按 priority 升序对模块表冒泡排序（调用方须持有非 NULL 的 state）
 */
static void _sort_modules(bm_module_cpu_state_t *state) {
    for (uint32_t i = 0; i < state->module_count; i++) {
        for (uint32_t j = i + 1; j < state->module_count; j++) {
            if (state->modules[i].priority > state->modules[j].priority) {
                bm_module_t tmp = state->modules[i];
                state->modules[i] = state->modules[j];
                state->modules[j] = tmp;
            }
        }
    }
}

/**
 * @brief init 失败时逆序回滚已初始化模块（调用方须持有非 NULL 的 state）
 */
static int _rollback_inits(bm_module_cpu_state_t *state, uint32_t through_index) {
    int rc = BM_OK;

    while (through_index > 0u) {
        through_index--;
        if (state->modules[through_index].state == BM_MODULE_STATE_INITED) {
            if (state->modules[through_index].deinit) {
                int r = state->modules[through_index].deinit();

                if (r != BM_OK) {
                    BM_LOGE("module", "init rollback failed idx=%u rc=%d",
                            (unsigned)through_index, r);
                    if (rc == BM_OK) {
                        rc = r;
                    }
                    continue;
                }
            }
            state->modules[through_index].state = BM_MODULE_STATE_UNINIT;
        }
    }
    return rc;
}

#if BM_CPU_LOCAL_ENABLE_ROUTE
/**
 * @brief start 失败时逆序停止已启动模块（调用方须持有非 NULL 的 state）
 *
 * 仅被 ROUTE 分支（bm_module_start_on_this_cpu 的 start 回滚）引用，
 * 非 ROUTE 构建下不编入以避免 unused 告警。
 */
static int _rollback_starts(bm_module_cpu_state_t *state, uint32_t through_index) {
    int rc = BM_OK;

    while (through_index > 0u) {
        through_index--;
        if (state->modules[through_index].state == BM_MODULE_STATE_STARTED) {
            if (state->modules[through_index].stop) {
                int r = state->modules[through_index].stop();

                if (r != BM_OK) {
                    BM_LOGE("module", "start rollback failed idx=%u rc=%d",
                            (unsigned)through_index, r);
                    if (rc == BM_OK) {
                        rc = r;
                    }
                    continue;
                }
            }
            state->modules[through_index].state = BM_MODULE_STATE_INITED;
        }
    }
    return rc;
}
#endif /* BM_CPU_LOCAL_ENABLE_ROUTE */

/**
 * @brief 从模块表加载并依次调用 init
 *
 * @return BM_OK 全部成功；负值为首个失败模块的错误码
 */
#if !BM_CPU_LOCAL_ENABLE_ROUTE
/**
 * @brief 初始化内部模块表中的全部模块
 *
 * @param reset_event_bus 是否先重置事件总线
 * @return BM_OK 全部成功；负值为初始化或状态错误
 */
static int _module_init_all(bool reset_event_bus) {
    bm_module_cpu_state_t *state = bm_module_this();
    bm_irq_state_t s;

    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    s = BM_CRITICAL_ENTER();
    if (state->initialized != BM_MODULES_UNINITIALIZED) {
        BM_CRITICAL_EXIT(s);
        BM_LOGW("module", "init_all already done");
        return BM_ERR_ALREADY;
    }
    state->initialized = BM_MODULES_INITIALIZING;
    BM_CRITICAL_EXIT(s);

    /*
     * 先校验模块表上界，通过后再 reset 事件总线。
     * 避免校验失败时已 reset 事件总线导致已发布事件不可逆丢失。
     */
    if (_bm_module_count > BM_CONFIG_MAX_MODULES) {
        BM_LOGE("module", "module table truncated: %u > %u",
                (unsigned)_bm_module_count, (unsigned)BM_CONFIG_MAX_MODULES);
        s = BM_CRITICAL_ENTER();
        state->initialized = BM_MODULES_UNINITIALIZED;
        BM_CRITICAL_EXIT(s);
        return BM_ERR_OVERFLOW;
    }

    /*
     * NULL 表项校验前移到 bm_event_reset() 之前（C1）：若在 reset 之后才
     * 发现 NULL 表项而失败返回，事件总线已被清空，注释所说"避免已发布
     * 事件不可逆丢失"的意图就落空了。故与上面的上界校验同侧，一并在
     * reset 前完成，reset 只在全部表项校验通过后才执行。
     */
    for (uint32_t i = 0u; i < _bm_module_count; i++) {
        if (_bm_module_table[i] == NULL) {
            BM_LOGE("module", "module table contains null entry idx=%u",
                    (unsigned)i);
            s = BM_CRITICAL_ENTER();
            state->module_count = 0u;
            state->initialized = BM_MODULES_UNINITIALIZED;
            BM_CRITICAL_EXIT(s);
            return BM_ERR_INVALID;
        }
    }

    if (reset_event_bus) {
        bm_event_reset();
    }

    state->module_count = _bm_module_count;
    for (uint32_t i = 0u; i < state->module_count; i++) {
        memcpy(&state->modules[i], _bm_module_table[i], sizeof(bm_module_t));
    }
    for (uint32_t i = 0u; i < state->module_count; i++) {
        state->modules[i].state = BM_MODULE_STATE_UNINIT;
    }
    _sort_modules(state);

    BM_LOGI("module", "init_all count=%u", (unsigned)state->module_count);
    for (uint32_t i = 0u; i < state->module_count; i++) {
        int r = state->modules[i].init ? state->modules[i].init() : BM_OK;

        if (r == BM_OK) {
            state->modules[i].state = BM_MODULE_STATE_INITED;
            BM_LOGD("module", "'%s' inited",
                    state->modules[i].name ? state->modules[i].name : "(null)");
        } else {
            int rollback_rc;

            BM_LOGE("module", "'%s' init failed rc=%d",
                    state->modules[i].name ? state->modules[i].name : "(null)", r);
            rollback_rc = _rollback_inits(state, i);
            if (rollback_rc != BM_OK) {
                s = BM_CRITICAL_ENTER();
                state->initialized = BM_MODULES_CLEANUP_PENDING;
                BM_CRITICAL_EXIT(s);
            } else {
                s = BM_CRITICAL_ENTER();
                state->module_count = 0u;
                state->initialized = BM_MODULES_UNINITIALIZED;
                BM_CRITICAL_EXIT(s);
            }
            return r;
        }
    }
    s = BM_CRITICAL_ENTER();
    state->initialized = BM_MODULES_READY;
    BM_CRITICAL_EXIT(s);
    /*
     * 冻结事件订阅表：流式运行期间订阅链表不可变，
     * 使 bm_event_process 的分发时间为编译期常量。
     * 同步冻结注册表。
     */
    bm_event_freeze_subscriptions();
    if (s_freeze_hook) {
        s_freeze_hook();
    }
    return BM_OK;
}
#endif

/**
 * @brief 引导启动：初始化并启动所有模块
 *
 * 默认路径使用；按 CPU 路由启用时返回 BM_ERR_INVALID。
 *
 * @return BM_OK 成功；负值表示失败
 */
int bm_module_boot(void) {
    /*
     * bm_module_boot 仅限 Bootstrap CPU 调用：
     * 内部调用 _module_init_all(true) 会 reset 事件总线并冻结订阅表——
     * 按 CPU 路由启用时若被非 owner CPU 调用，将破坏该域的事件状态。
     * 按 CPU 路由的场景请使用 bm_module_init_on_this_cpu()。
 */
#if BM_CPU_LOCAL_ENABLE_ROUTE
    BM_LOGE("module", "bm_module_boot not supported when CPU routing is enabled;"
            " use bm_module_init_on_this_cpu() on the owner CPU");
    return BM_ERR_INVALID;
#else
    int r = _module_init_all(true);

    if (r != BM_OK) {
        return r;
    }
    return bm_module_start_all();
#endif
}

/**
 * @brief 初始化所有模块（不启动）
 *
 * 默认路径使用；按 CPU 路由启用时返回 BM_ERR_INVALID。
 *
 * @return BM_OK 成功；负值表示失败
 */
int bm_module_init_all(void) {
#if BM_CPU_LOCAL_ENABLE_ROUTE
    BM_LOGW("module", "init_all not supported when CPU routing is enabled;"
            " use bm_module_init_on_this_cpu() on the owner CPU");
    return BM_ERR_INVALID;
#else
    return _module_init_all(false);
#endif
}

#if !BM_CPU_LOCAL_ENABLE_ROUTE
/**
 * @brief 判断模块是否匹配指定域（含 COMMON 通配域）
 *
 * @param mod    模块描述符（不可为 NULL）
 * @param domain 目标域
 * @return 非零表示匹配
 */
static int _module_domain_matches(const bm_module_t *mod, bm_domain_t domain) {
    return mod->domain == domain || mod->domain == BM_DOMAIN_COMMON;
}

/**
 * @brief 启动 state 中满足域过滤条件的已初始化模块
 *
 * @param state  当前 CPU 模块状态
 * @param domain 目标域（filter 为 0 时忽略）
 * @param filter 0 表示处理全部模块；非零表示仅处理匹配 domain 的模块
 * @return BM_OK 成功；负值为首个失败模块的错误码
 */
static int _start_modules_filtered(bm_module_cpu_state_t *state,
                                   bm_domain_t domain,
                                   int filter) {
    for (uint32_t i = 0u; i < state->module_count; i++) {
        if (filter && !_module_domain_matches(&state->modules[i], domain)) {
            continue;
        }
        if (state->modules[i].state == BM_MODULE_STATE_INITED ||
            state->modules[i].state == BM_MODULE_STATE_STOPPED) {
            int r = state->modules[i].start ? state->modules[i].start() : BM_OK;

            if (r == BM_OK) {
                state->modules[i].state = BM_MODULE_STATE_STARTED;
                BM_LOGD("module", "'%s' started",
                        state->modules[i].name ? state->modules[i].name : "(null)");
            } else {
                BM_LOGE("module", "'%s' start failed rc=%d",
                        state->modules[i].name ? state->modules[i].name : "(null)", r);
                return r;
            }
        }
    }
    return BM_OK;
}

/**
 * @brief start 失败时逆序停止已启动的模块，可按域过滤
 *
 * @param state          当前 CPU 模块状态
 * @param through_index  回滚上限索引（不包含）
 * @param domain         目标域（filter 为 0 时忽略）
 * @param filter         0 表示处理全部模块；非零表示仅处理匹配 domain 的模块
 * @return BM_OK 全部成功；负值为首个失败模块的错误码
 */
static int _rollback_starts_filtered(bm_module_cpu_state_t *state,
                                     uint32_t through_index,
                                     bm_domain_t domain,
                                     int filter) {
    int rc = BM_OK;

    while (through_index > 0u) {
        through_index--;
        if (filter && !_module_domain_matches(&state->modules[through_index], domain)) {
            continue;
        }
        if (state->modules[through_index].state == BM_MODULE_STATE_STARTED) {
            if (state->modules[through_index].stop) {
                int r = state->modules[through_index].stop();

                if (r != BM_OK) {
                    BM_LOGE("module", "start rollback failed idx=%u rc=%d",
                            (unsigned)through_index, r);
                    if (rc == BM_OK) {
                        rc = r;
                    }
                    continue;
                }
            }
            state->modules[through_index].state = BM_MODULE_STATE_INITED;
        }
    }
    return rc;
}

/**
 * @brief 停止 state 中满足域过滤条件的已启动模块
 *
 * @param state  当前 CPU 模块状态
 * @param domain 目标域（filter 为 0 时忽略）
 * @param filter 0 表示处理全部模块；非零表示仅处理匹配 domain 的模块
 * @return BM_OK 全部成功；负值为首个失败模块的错误码
 */
static int _stop_modules_filtered(bm_module_cpu_state_t *state,
                                  bm_domain_t domain,
                                  int filter) {
    int rc = BM_OK;

    for (int i = (int)state->module_count - 1; i >= 0; i--) {
        if (filter && !_module_domain_matches(&state->modules[i], domain)) {
            continue;
        }
        if (state->modules[i].state == BM_MODULE_STATE_STARTED) {
            int r = state->modules[i].stop ? state->modules[i].stop() : BM_OK;

            if (r == BM_OK) {
                state->modules[i].state = BM_MODULE_STATE_STOPPED;
            } else {
                BM_LOGE("module", "stop failed idx=%d rc=%d", i, r);
                if (rc == BM_OK) {
                    rc = r;
                }
            }
        }
    }
    return rc;
}

/**
 * @brief 反初始化 state 中满足域过滤条件的模块
 *
 * @param state        当前 CPU 模块状态
 * @param domain       目标域（filter 为 0 时忽略）
 * @param filter       0 表示处理全部模块；非零表示仅处理匹配 domain 的模块
 * @param remaining    输出参数：返回未被反初始化的模块数（filter 为 0 时恒为 0）
 * @return BM_OK 全部成功；负值为首个失败模块的错误码
 */
static int _deinit_modules_filtered(bm_module_cpu_state_t *state,
                                    bm_domain_t domain,
                                    int filter,
                                    uint32_t *remaining) {
    int rc = BM_OK;
    uint32_t rem = 0u;

    for (int i = (int)state->module_count - 1; i >= 0; i--) {
        if (filter && !_module_domain_matches(&state->modules[i], domain)) {
            if (state->modules[i].state != BM_MODULE_STATE_UNINIT) {
                rem++;
            }
            continue;
        }
        if (state->modules[i].state == BM_MODULE_STATE_STARTED) {
            int r = state->modules[i].stop ? state->modules[i].stop() : BM_OK;

            if (r != BM_OK) {
                BM_LOGE("module", "deinit stop failed idx=%d rc=%d", i, r);
                if (rc == BM_OK) {
                    rc = r;
                }
                rem++;
                continue;
            }
            state->modules[i].state = BM_MODULE_STATE_STOPPED;
        }
        if (state->modules[i].state != BM_MODULE_STATE_UNINIT &&
            state->modules[i].deinit) {
            int r = state->modules[i].deinit();
            if (r != BM_OK) {
                if (rc == BM_OK) {
                    rc = r;
                }
                rem++;
                continue;
            }
        }
        state->modules[i].state = BM_MODULE_STATE_UNINIT;
    }
    if (remaining != NULL) {
        *remaining = rem;
    }
    return rc;
}
#endif /* !BM_CPU_LOCAL_ENABLE_ROUTE */

/**
 * @brief 依次启动已初始化的模块
 *
 * @return BM_OK 全部成功；负值为首个失败模块的错误码
 */
int bm_module_start_all(void) {
#if BM_CPU_LOCAL_ENABLE_ROUTE
    BM_LOGW("module", "start_all not supported when CPU routing is enabled;"
            " use bm_module_start_on_this_cpu() on the owner CPU");
    return BM_ERR_INVALID;
#else
    bm_module_cpu_state_t *state = bm_module_this();
    bm_irq_state_t s;
    int initialized;
    int rc;

    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    s = BM_CRITICAL_ENTER();
    initialized = state->initialized;

    if (initialized == BM_MODULES_INITIALIZING ||
        initialized == BM_MODULES_TRANSITIONING ||
        initialized == BM_MODULES_CLEANUP_PENDING) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_BUSY;
    }
    if (initialized != BM_MODULES_READY) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_NOT_INIT;
    }
    state->initialized = BM_MODULES_TRANSITIONING;
    BM_CRITICAL_EXIT(s);

    rc = _start_modules_filtered(state, (bm_domain_t)0, 0);
    if (rc != BM_OK) {
        if (_rollback_starts_filtered(state, state->module_count, (bm_domain_t)0, 0)
                != BM_OK) {
            s = BM_CRITICAL_ENTER();
            state->initialized = BM_MODULES_CLEANUP_PENDING;
            BM_CRITICAL_EXIT(s);
        } else {
            s = BM_CRITICAL_ENTER();
            state->initialized = BM_MODULES_READY;
            BM_CRITICAL_EXIT(s);
        }
        return rc;
    }
    s = BM_CRITICAL_ENTER();
    state->initialized = BM_MODULES_READY;
    BM_CRITICAL_EXIT(s);
    return BM_OK;
#endif
}

/**
 * @brief 逆序停止已启动的模块
 *
 * @return BM_OK 全部成功；负值为首个失败模块的错误码
 */
int bm_module_stop_all(void) {
#if BM_CPU_LOCAL_ENABLE_ROUTE
    BM_LOGW("module", "stop_all not supported when CPU routing is enabled;"
            " stop modules on each CPU individually");
    return BM_ERR_INVALID;
#else
    bm_module_cpu_state_t *state = bm_module_this();
    bm_irq_state_t s;
    int rc;

    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    s = BM_CRITICAL_ENTER();
    if (state->initialized == BM_MODULES_INITIALIZING ||
        state->initialized == BM_MODULES_TRANSITIONING ||
        state->initialized == BM_MODULES_CLEANUP_PENDING) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_BUSY;
    }
    if (state->initialized == BM_MODULES_UNINITIALIZED) {
        BM_CRITICAL_EXIT(s);
        return BM_OK;
    }
    state->initialized = BM_MODULES_TRANSITIONING;
    BM_CRITICAL_EXIT(s);

    rc = _stop_modules_filtered(state, (bm_domain_t)0, 0);
    s = BM_CRITICAL_ENTER();
    state->initialized = BM_MODULES_READY;
    BM_CRITICAL_EXIT(s);
    BM_LOGI("module", "stop_all done");
    return rc;
#endif
}

/**
 * @brief 逆序反初始化所有模块
 *
 * @return BM_OK 全部成功；负值为首个失败模块的错误码
 */
int bm_module_deinit_all(void) {
#if BM_CPU_LOCAL_ENABLE_ROUTE
    BM_LOGW("module", "deinit_all not supported when CPU routing is enabled;"
            " deinit modules on each CPU individually");
    return BM_ERR_INVALID;
#else
    bm_module_cpu_state_t *state = bm_module_this();
    bm_irq_state_t s;
    int rc;

    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    s = BM_CRITICAL_ENTER();
    if (state->initialized == BM_MODULES_INITIALIZING ||
        state->initialized == BM_MODULES_TRANSITIONING) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_BUSY;
    }
    state->initialized = BM_MODULES_CLEANUP_PENDING;
    BM_CRITICAL_EXIT(s);

    rc = _deinit_modules_filtered(state, (bm_domain_t)0, 0, NULL);
    if (rc != BM_OK) {
        BM_LOGW("module", "deinit_all completed with errors rc=%d", rc);
    } else {
        s = BM_CRITICAL_ENTER();
        state->initialized = BM_MODULES_UNINITIALIZED;
        state->module_count = 0u;
        BM_CRITICAL_EXIT(s);
        BM_LOGI("module", "deinit_all done");
    }
    return rc;
#endif
}

/**
 * @brief 仅复制匹配 domain 或 COMMON 的模块到内部工作表并初始化
 */
#if !BM_CPU_LOCAL_ENABLE_ROUTE
/**
 * @brief 初始化匹配指定执行域或 COMMON 域的模块
 *
 * @param domain 目标执行域
 * @param reset_event_bus 是否先重置事件总线
 * @return BM_OK 全部成功；负值为初始化或状态错误
 */
static int _module_init_all_for_domain(bm_domain_t domain, bool reset_event_bus) {
    bm_module_cpu_state_t *state = bm_module_this();
    bm_irq_state_t s;

    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    s = BM_CRITICAL_ENTER();
    if (state->initialized != BM_MODULES_UNINITIALIZED) {
        BM_CRITICAL_EXIT(s);
        BM_LOGW("module", "init_all_for_domain already done");
        return BM_ERR_ALREADY;
    }
    state->initialized = BM_MODULES_INITIALIZING;
    BM_CRITICAL_EXIT(s);

    /*
     * 先校验模块表上界，通过后再 reset 事件总线。
     * 避免校验失败时已 reset 事件总线导致已发布事件不可逆丢失。
     */
    if (_bm_module_count > BM_CONFIG_MAX_MODULES) {
        BM_LOGE("module", "module table truncated: %u > %u",
                (unsigned)_bm_module_count, (unsigned)BM_CONFIG_MAX_MODULES);
        s = BM_CRITICAL_ENTER();
        state->initialized = BM_MODULES_UNINITIALIZED;
        BM_CRITICAL_EXIT(s);
        return BM_ERR_OVERFLOW;
    }

    /*
     * NULL 表项校验前移到 bm_event_reset() 之前（C1，与 _module_init_all
     * 同理）：避免 reset 之后才发现 NULL 表项失败返回，导致已发布事件
     * 不可逆丢失。reset 只在全部表项校验通过后才执行。
     */
    for (uint32_t i = 0u; i < _bm_module_count; i++) {
        if (_bm_module_table[i] == NULL) {
            BM_LOGE("module", "module table contains null entry idx=%u",
                    (unsigned)i);
            s = BM_CRITICAL_ENTER();
            state->module_count = 0u;
            state->initialized = BM_MODULES_UNINITIALIZED;
            BM_CRITICAL_EXIT(s);
            return BM_ERR_INVALID;
        }
    }

    if (reset_event_bus) {
        bm_event_reset();
    }

    state->module_count = 0;
    for (uint32_t i = 0u; i < _bm_module_count; i++) {
        if (_module_domain_matches(_bm_module_table[i], domain)) {
            memcpy(&state->modules[state->module_count], _bm_module_table[i],
                   sizeof(bm_module_t));
            state->modules[state->module_count].state = BM_MODULE_STATE_UNINIT;
            state->module_count++;
        }
    }

    _sort_modules(state);

    BM_LOGI("module", "init_all_for_domain count=%u",
            (unsigned)state->module_count);
    for (uint32_t i = 0u; i < state->module_count; i++) {
        int r = state->modules[i].init ? state->modules[i].init() : BM_OK;

        if (r == BM_OK) {
            state->modules[i].state = BM_MODULE_STATE_INITED;
            BM_LOGD("module", "'%s' inited",
                    state->modules[i].name ? state->modules[i].name : "(null)");
        } else {
            int rollback_rc;

            BM_LOGE("module", "'%s' init failed rc=%d",
                    state->modules[i].name ? state->modules[i].name : "(null)", r);
            rollback_rc = _rollback_inits(state, i);
            if (rollback_rc != BM_OK) {
                s = BM_CRITICAL_ENTER();
                state->initialized = BM_MODULES_CLEANUP_PENDING;
                BM_CRITICAL_EXIT(s);
            } else {
                s = BM_CRITICAL_ENTER();
                state->module_count = 0u;
                state->initialized = BM_MODULES_UNINITIALIZED;
                BM_CRITICAL_EXIT(s);
            }
            return r;
        }
    }
    s = BM_CRITICAL_ENTER();
    state->initialized = BM_MODULES_READY;
    BM_CRITICAL_EXIT(s);
    bm_event_freeze_subscriptions();
    if (s_freeze_hook) {
        s_freeze_hook();
    }
    return BM_OK;
}
#endif

/**
 * @brief 初始化匹配指定 domain 的模块（不启动）
 *
 * @param domain 目标域
 * @return BM_OK 成功；负值表示失败
 */
int bm_module_init_all_for_domain(bm_domain_t domain) {
#if BM_CPU_LOCAL_ENABLE_ROUTE
    BM_LOGW("module", "init_all_for_domain not supported when CPU routing is enabled;"
            " use bm_module_init_on_this_cpu() on the owner CPU");
    (void)domain;
    return BM_ERR_INVALID;
#else
    return _module_init_all_for_domain(domain, false);
#endif
}

/**
 * @brief 启动匹配指定 domain 的已初始化模块
 *
 * @param domain 目标域
 * @return BM_OK 成功；负值表示失败
 */
int bm_module_start_all_for_domain(bm_domain_t domain) {
#if BM_CPU_LOCAL_ENABLE_ROUTE
    BM_LOGW("module", "start_all_for_domain not supported when CPU routing is enabled;"
            " use bm_module_start_on_this_cpu() on the owner CPU");
    (void)domain;
    return BM_ERR_INVALID;
#else
    bm_module_cpu_state_t *state = bm_module_this();
    bm_irq_state_t s;
    int initialized;
    int rc;

    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    s = BM_CRITICAL_ENTER();
    initialized = state->initialized;

    if (initialized == BM_MODULES_INITIALIZING ||
        initialized == BM_MODULES_TRANSITIONING ||
        initialized == BM_MODULES_CLEANUP_PENDING) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_BUSY;
    }
    if (initialized != BM_MODULES_READY) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_NOT_INIT;
    }
    state->initialized = BM_MODULES_TRANSITIONING;
    BM_CRITICAL_EXIT(s);

    rc = _start_modules_filtered(state, domain, 1);
    if (rc != BM_OK) {
        if (_rollback_starts_filtered(state, state->module_count, domain, 1)
                != BM_OK) {
            s = BM_CRITICAL_ENTER();
            state->initialized = BM_MODULES_CLEANUP_PENDING;
            BM_CRITICAL_EXIT(s);
        } else {
            s = BM_CRITICAL_ENTER();
            state->initialized = BM_MODULES_READY;
            BM_CRITICAL_EXIT(s);
        }
        return rc;
    }
    s = BM_CRITICAL_ENTER();
    state->initialized = BM_MODULES_READY;
    BM_CRITICAL_EXIT(s);
    return BM_OK;
#endif
}

/**
 * @brief 停止匹配指定 domain 的已启动模块
 *
 * @param domain 目标域
 * @return BM_OK 成功；负值表示失败
 */
int bm_module_stop_all_for_domain(bm_domain_t domain) {
#if BM_CPU_LOCAL_ENABLE_ROUTE
    BM_LOGW("module", "stop_all_for_domain not supported when CPU routing is enabled;"
            " stop modules on each CPU individually");
    (void)domain;
    return BM_ERR_INVALID;
#else
    bm_module_cpu_state_t *state = bm_module_this();
    bm_irq_state_t s;
    int rc;
    int initialized;

    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    s = BM_CRITICAL_ENTER();
    initialized = state->initialized;
    if (initialized == BM_MODULES_INITIALIZING ||
        initialized == BM_MODULES_TRANSITIONING ||
        initialized == BM_MODULES_CLEANUP_PENDING) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_BUSY;
    }
    if (initialized == BM_MODULES_UNINITIALIZED) {
        BM_CRITICAL_EXIT(s);
        return BM_OK;
    }
    state->initialized = BM_MODULES_TRANSITIONING;
    BM_CRITICAL_EXIT(s);

    rc = _stop_modules_filtered(state, domain, 1);
    s = BM_CRITICAL_ENTER();
    state->initialized = BM_MODULES_READY;
    BM_CRITICAL_EXIT(s);
    BM_LOGI("module", "stop_all_for_domain done");
    return rc;
#endif
}

/**
 * @brief 反初始化匹配指定 domain 的模块
 *
 * @param domain 目标域
 * @return BM_OK 成功；负值表示失败
 */
int bm_module_deinit_all_for_domain(bm_domain_t domain) {
#if BM_CPU_LOCAL_ENABLE_ROUTE
    BM_LOGW("module", "deinit_all_for_domain not supported when CPU routing is enabled;"
            " deinit modules on each CPU individually");
    (void)domain;
    return BM_ERR_INVALID;
#else
    bm_module_cpu_state_t *state = bm_module_this();
    bm_irq_state_t s;
    int rc;
    int initialized;
    uint32_t remaining = 0u;

    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    s = BM_CRITICAL_ENTER();
    initialized = state->initialized;
    if (initialized == BM_MODULES_INITIALIZING ||
        initialized == BM_MODULES_TRANSITIONING) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_BUSY;
    }
    state->initialized = BM_MODULES_CLEANUP_PENDING;
    BM_CRITICAL_EXIT(s);

    rc = _deinit_modules_filtered(state, domain, 1, &remaining);
    if (rc != BM_OK) {
        BM_LOGW("module", "deinit_all_for_domain completed with errors rc=%d", rc);
    } else {
        s = BM_CRITICAL_ENTER();
        if (remaining == 0u) {
            state->initialized = BM_MODULES_UNINITIALIZED;
            state->module_count = 0u;
        } else {
            state->initialized = BM_MODULES_READY;
        }
        BM_CRITICAL_EXIT(s);
        BM_LOGI("module", "deinit_all_for_domain done");
    }
    return rc;
#endif
}

/**
 * @brief 在当前 CPU 上初始化属于该核的模块
 *
 * 按 CPU 路由时根据 s_owner_resolver 过滤模块表。
 *
 * @return BM_OK 成功；负值表示失败
 */
int bm_module_init_on_this_cpu(void) {
#if !BM_CPU_LOCAL_ENABLE_ROUTE
    return bm_module_init_all();
#else
    bm_module_cpu_state_t *state = bm_module_this();
    bm_irq_state_t s;
    uint32_t i;

    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    if (!s_owner_resolver) {
        return BM_ERR_NOT_INIT;
    }

    s = BM_CRITICAL_ENTER();
    if (state->initialized != BM_MODULES_UNINITIALIZED) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_ALREADY;
    }
    state->initialized = BM_MODULES_INITIALIZING;
    BM_CRITICAL_EXIT(s);

    if (_bm_module_count > BM_CONFIG_MAX_MODULES) {
        s = BM_CRITICAL_ENTER();
        state->initialized = BM_MODULES_UNINITIALIZED;
        BM_CRITICAL_EXIT(s);
        return BM_ERR_OVERFLOW;
    }

    state->module_count = 0u;
    for (i = 0u; i < _bm_module_count; i++) {
        if (_bm_module_table[i] == NULL) {
            s = BM_CRITICAL_ENTER();
            state->initialized = BM_MODULES_UNINITIALIZED;
            BM_CRITICAL_EXIT(s);
            return BM_ERR_INVALID;
        }
        {
            uint8_t owner = s_owner_resolver(i);
            /* BM_CPU_ANY 表示“在所有 CPU 上运行”；每个 CPU 均应包含该模块。 */
            if (owner != BM_CPU_ANY &&
                owner != (uint8_t)BM_CPU_THIS()) {
                continue;
            }
        }
        memcpy(&state->modules[state->module_count], _bm_module_table[i],
               sizeof(bm_module_t));
        state->modules[state->module_count].state = BM_MODULE_STATE_UNINIT;
        state->module_count++;
    }

    _sort_modules(state);
    BM_LOGI("module", "init_on_this_cpu count=%u",
            (unsigned)state->module_count);
    for (i = 0u; i < state->module_count; i++) {
        int r = state->modules[i].init ? state->modules[i].init() : BM_OK;

        if (r != BM_OK) {
            /*
             * 接住回滚返回值：回滚失败（个别模块 deinit 未成功）不能静默
             * 复位为 UNINITIALIZED——那会让调用方误以为可以从头重新
             * init，实际上部分模块残留 INITED 状态。与非路由路径
             * _module_init_all 对齐：回滚失败置 CLEANUP_PENDING（A2）。
             */
            int rollback_rc = _rollback_inits(state, i);

            s = BM_CRITICAL_ENTER();
            if (rollback_rc != BM_OK) {
                state->initialized = BM_MODULES_CLEANUP_PENDING;
            } else {
                state->module_count = 0u;
                state->initialized = BM_MODULES_UNINITIALIZED;
            }
            BM_CRITICAL_EXIT(s);
            return r;
        }
        state->modules[i].state = BM_MODULE_STATE_INITED;
    }
    s = BM_CRITICAL_ENTER();
    state->initialized = BM_MODULES_READY;
    BM_CRITICAL_EXIT(s);
    /*
     * 冻结本 CPU 的订阅表与注册表。按 CPU 路由时，两者均操作
     * per-CPU 状态，可从各 CPU init 路径并发安全调用。
     */
    bm_event_freeze_subscriptions();
    if (s_freeze_hook) {
        s_freeze_hook();
    }
    return BM_OK;
#endif
}

/**
 * @brief 在当前 CPU 上启动已初始化的模块
 *
 * @return BM_OK 成功；负值表示失败
 */
int bm_module_start_on_this_cpu(void) {
#if !BM_CPU_LOCAL_ENABLE_ROUTE
    return bm_module_start_all();
#else
    bm_module_cpu_state_t *state = bm_module_this();
    bm_irq_state_t s;
    uint32_t i;

    if (state == NULL) {
        return BM_ERR_INVALID;
    }

    s = BM_CRITICAL_ENTER();
    /*
     * 状态码语义对齐非路由路径 bm_module_start_all（A3）：
     * INITIALIZING/TRANSITIONING/CLEANUP_PENDING 属"忙态"（另一流程正在
     * 变更状态），应返回 BM_ERR_BUSY 而非笼统的 BM_ERR_NOT_INIT；
     * 仅 UNINITIALIZED（尚未 init）才是真正的"未初始化"。
     */
    if (state->initialized == BM_MODULES_INITIALIZING ||
        state->initialized == BM_MODULES_TRANSITIONING ||
        state->initialized == BM_MODULES_CLEANUP_PENDING) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_BUSY;
    }
    if (state->initialized != BM_MODULES_READY) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_NOT_INIT;
    }
    state->initialized = BM_MODULES_TRANSITIONING;
    BM_CRITICAL_EXIT(s);

    for (i = 0u; i < state->module_count; i++) {
        if (state->modules[i].state == BM_MODULE_STATE_INITED ||
            state->modules[i].state == BM_MODULE_STATE_STOPPED) {
            int r = state->modules[i].start ? state->modules[i].start() : BM_OK;

            if (r != BM_OK) {
                /*
                 * 接住回滚返回值：回滚失败不能静默复位为 READY——那会让
                 * 调用方误以为可以立即重新 start，实际上部分模块残留
                 * STARTED 状态。与非路由路径 bm_module_start_all 对齐：
                 * 回滚失败置 CLEANUP_PENDING（A2）。
                 */
                int rollback_rc = _rollback_starts(state, i);

                s = BM_CRITICAL_ENTER();
                state->initialized = (rollback_rc != BM_OK) ?
                    BM_MODULES_CLEANUP_PENDING : BM_MODULES_READY;
                BM_CRITICAL_EXIT(s);
                return r;
            }
            state->modules[i].state = BM_MODULE_STATE_STARTED;
        }
    }
    s = BM_CRITICAL_ENTER();
    state->initialized = BM_MODULES_READY;
    BM_CRITICAL_EXIT(s);
    return BM_OK;
#endif
}
