/**
 * @file bm_module.c
 * @brief 模块生命周期管理实现
 *
 * 从应用提供的 _bm_module_table 加载模块，按优先级排序后依次 init/start/stop/deinit。
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-10       1.1            zeh            失败回滚与状态机加固
 *
 */
#include "bm_module.h"
#include "bm/core/bm_module_domain.h"
#include "bm_critical_wrap.h"
#include "bm_event.h"
#include "bm_log.h"
#include "bm/core/bm_cpu_local.h"
#include "hal/bm_hal_cpu.h"

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

typedef struct {
    bm_module_cpu_state_t state;
    uint8_t padding[BM_CONFIG_CACHE_LINE -
                    (sizeof(bm_module_cpu_state_t) % BM_CONFIG_CACHE_LINE)];
} bm_module_cpu_storage_t;

static BM_CACHE_ALIGNAS(BM_CONFIG_CACHE_LINE)
bm_module_cpu_storage_t g_module_cpu[BM_CONFIG_CPU_COUNT];
static bm_module_owner_resolver_t s_owner_resolver;

static bm_module_cpu_state_t *bm_module_this(void) {
    uint32_t cpu = BM_CPU_THIS();

    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return NULL;
    }
    return &g_module_cpu[cpu].state;
}

#if BM_CPU_LOCAL_ENABLE_ROUTE
static int module_require_cpu(void) {
    if (bm_module_this() == NULL) {
        BM_LOGE("module", "invalid cpu %u", (unsigned)BM_CPU_THIS());
        return BM_ERR_INVALID;
    }
    return BM_OK;
}
#endif

#define _modules             (bm_module_this()->modules)
#define _module_count        (bm_module_this()->module_count)
#define _modules_initialized (bm_module_this()->initialized)

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

/**
 * @brief 按 priority 升序对模块表冒泡排序
 */
static void _sort_modules(void) {
    for (uint32_t i = 0; i < _module_count; i++) {
        for (uint32_t j = i + 1; j < _module_count; j++) {
            if (_modules[i].priority > _modules[j].priority) {
                bm_module_t tmp = _modules[i];
                _modules[i] = _modules[j];
                _modules[j] = tmp;
            }
        }
    }
}

/**
 * @brief init 失败时逆序回滚已初始化模块
 */
static int _rollback_inits(uint32_t through_index) {
    int rc = BM_OK;

    while (through_index > 0u) {
        through_index--;
        if (_modules[through_index].state == BM_MODULE_STATE_INITED) {
            if (_modules[through_index].deinit) {
                int r = _modules[through_index].deinit();

                if (r != BM_OK) {
                    BM_LOGE("module", "init rollback failed idx=%u rc=%d",
                            (unsigned)through_index, r);
                    if (rc == BM_OK) {
                        rc = r;
                    }
                    continue;
                }
            }
            _modules[through_index].state = BM_MODULE_STATE_UNINIT;
        }
    }
    return rc;
}

/**
 * @brief start 失败时逆序停止已启动模块
 */
static int _rollback_starts(uint32_t through_index) {
    int rc = BM_OK;

    while (through_index > 0u) {
        through_index--;
        if (_modules[through_index].state == BM_MODULE_STATE_STARTED) {
            if (_modules[through_index].stop) {
                int r = _modules[through_index].stop();

                if (r != BM_OK) {
                    BM_LOGE("module", "start rollback failed idx=%u rc=%d",
                            (unsigned)through_index, r);
                    if (rc == BM_OK) {
                        rc = r;
                    }
                    continue;
                }
            }
            _modules[through_index].state = BM_MODULE_STATE_INITED;
        }
    }
    return rc;
}

/**
 * @brief 从模块表加载并依次调用 init
 *
 * @return BM_OK 全部成功；负值为首个失败模块的错误码
 */
#if !BM_CPU_LOCAL_ENABLE_ROUTE
static int _module_init_all(bool reset_event_bus) {
    bm_irq_state_t s = BM_CRITICAL_ENTER();

    if (_modules_initialized != BM_MODULES_UNINITIALIZED) {
        BM_CRITICAL_EXIT(s);
        BM_LOGW("module", "init_all already done");
        return BM_ERR_ALREADY;
    }
    _modules_initialized = BM_MODULES_INITIALIZING;
    BM_CRITICAL_EXIT(s);

    /*
     * 先校验模块表上界，通过后再 reset 事件总线。
     * 避免校验失败时已 reset 事件总线导致已发布事件不可逆丢失。
     */
    if (_bm_module_count > BM_CONFIG_MAX_MODULES) {
        BM_LOGE("module", "module table truncated: %u > %u",
                (unsigned)_bm_module_count, (unsigned)BM_CONFIG_MAX_MODULES);
        s = BM_CRITICAL_ENTER();
        _modules_initialized = BM_MODULES_UNINITIALIZED;
        BM_CRITICAL_EXIT(s);
        return BM_ERR_OVERFLOW;
    }

    if (reset_event_bus) {
        bm_event_reset();
    }

    _module_count = _bm_module_count;
    for (uint32_t i = 0u; i < _module_count; i++) {
        if (_bm_module_table[i] == NULL) {
            BM_LOGE("module", "module table contains null entry idx=%u",
                    (unsigned)i);
            s = BM_CRITICAL_ENTER();
            _module_count = 0u;
            _modules_initialized = BM_MODULES_UNINITIALIZED;
            BM_CRITICAL_EXIT(s);
            return BM_ERR_INVALID;
        }
        memcpy(&_modules[i], _bm_module_table[i], sizeof(bm_module_t));
    }
    for (uint32_t i = 0u; i < _module_count; i++) {
        _modules[i].state = BM_MODULE_STATE_UNINIT;
    }
    _sort_modules();

    BM_LOGI("module", "init_all count=%u", (unsigned)_module_count);
    for (uint32_t i = 0u; i < _module_count; i++) {
        int r = _modules[i].init ? _modules[i].init() : BM_OK;

        if (r == BM_OK) {
            _modules[i].state = BM_MODULE_STATE_INITED;
            BM_LOGD("module", "'%s' inited",
                    _modules[i].name ? _modules[i].name : "(null)");
        } else {
            int rollback_rc;

            BM_LOGE("module", "'%s' init failed rc=%d",
                    _modules[i].name ? _modules[i].name : "(null)", r);
            rollback_rc = _rollback_inits(i);
            if (rollback_rc != BM_OK) {
                s = BM_CRITICAL_ENTER();
                _modules_initialized = BM_MODULES_CLEANUP_PENDING;
                BM_CRITICAL_EXIT(s);
            } else {
                s = BM_CRITICAL_ENTER();
                _module_count = 0u;
                _modules_initialized = BM_MODULES_UNINITIALIZED;
                BM_CRITICAL_EXIT(s);
            }
            return r;
        }
    }
    s = BM_CRITICAL_ENTER();
    _modules_initialized = BM_MODULES_READY;
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
    bm_irq_state_t s = BM_CRITICAL_ENTER();
    int initialized = _modules_initialized;

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
    _modules_initialized = BM_MODULES_TRANSITIONING;
    BM_CRITICAL_EXIT(s);

    for (uint32_t i = 0u; i < _module_count; i++) {
        if (_modules[i].state == BM_MODULE_STATE_INITED ||
            _modules[i].state == BM_MODULE_STATE_STOPPED) {
            int r = _modules[i].start ? _modules[i].start() : BM_OK;

            if (r == BM_OK) {
                _modules[i].state = BM_MODULE_STATE_STARTED;
                BM_LOGD("module", "'%s' started",
                        _modules[i].name ? _modules[i].name : "(null)");
            } else {
                BM_LOGE("module", "'%s' start failed rc=%d",
                        _modules[i].name ? _modules[i].name : "(null)", r);
                if (_rollback_starts(i) != BM_OK) {
                    s = BM_CRITICAL_ENTER();
                    _modules_initialized = BM_MODULES_CLEANUP_PENDING;
                    BM_CRITICAL_EXIT(s);
                } else {
                    s = BM_CRITICAL_ENTER();
                    _modules_initialized = BM_MODULES_READY;
                    BM_CRITICAL_EXIT(s);
                }
                return r;
            }
        }
    }
    s = BM_CRITICAL_ENTER();
    _modules_initialized = BM_MODULES_READY;
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
    bm_irq_state_t s = BM_CRITICAL_ENTER();
    int rc = BM_OK;

    if (_modules_initialized == BM_MODULES_INITIALIZING ||
        _modules_initialized == BM_MODULES_TRANSITIONING ||
        _modules_initialized == BM_MODULES_CLEANUP_PENDING) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_BUSY;
    }
    if (_modules_initialized == BM_MODULES_UNINITIALIZED) {
        BM_CRITICAL_EXIT(s);
        return BM_OK;
    }
    _modules_initialized = BM_MODULES_TRANSITIONING;
    BM_CRITICAL_EXIT(s);

    for (int i = (int)_module_count - 1; i >= 0; i--) {
        if (_modules[i].state == BM_MODULE_STATE_STARTED) {
            int r = _modules[i].stop ? _modules[i].stop() : BM_OK;

            if (r == BM_OK) {
                _modules[i].state = BM_MODULE_STATE_STOPPED;
            } else {
                BM_LOGE("module", "stop failed idx=%d rc=%d", i, r);
                if (rc == BM_OK) {
                    rc = r;
                }
            }
        }
    }
    s = BM_CRITICAL_ENTER();
    _modules_initialized = BM_MODULES_READY;
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
    bm_irq_state_t s;
    int rc = BM_OK;

    s = BM_CRITICAL_ENTER();
    if (_modules_initialized == BM_MODULES_INITIALIZING ||
        _modules_initialized == BM_MODULES_TRANSITIONING) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_BUSY;
    }
    _modules_initialized = BM_MODULES_CLEANUP_PENDING;
    BM_CRITICAL_EXIT(s);

    for (int i = (int)_module_count - 1; i >= 0; i--) {
        if (_modules[i].state == BM_MODULE_STATE_STARTED) {
            int r = _modules[i].stop ? _modules[i].stop() : BM_OK;

            if (r != BM_OK) {
                BM_LOGE("module", "deinit stop failed idx=%d rc=%d", i, r);
                if (rc == BM_OK) {
                    rc = r;
                }
                continue;
            }
            _modules[i].state = BM_MODULE_STATE_STOPPED;
        }
        if (_modules[i].state != BM_MODULE_STATE_UNINIT &&
            _modules[i].deinit) {
            int r = _modules[i].deinit();
            if (r != BM_OK) {
                if (rc == BM_OK) {
                    rc = r;
                }
                continue;
            }
        }
        _modules[i].state = BM_MODULE_STATE_UNINIT;
    }
    if (rc != BM_OK) {
        s = BM_CRITICAL_ENTER();
        _modules_initialized = BM_MODULES_CLEANUP_PENDING;
        BM_CRITICAL_EXIT(s);
        BM_LOGW("module", "deinit_all completed with errors rc=%d", rc);
    } else {
        s = BM_CRITICAL_ENTER();
        _modules_initialized = BM_MODULES_UNINITIALIZED;
        _module_count = 0u;
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
static int _module_init_all_for_domain(bm_domain_t domain, bool reset_event_bus) {
    bm_irq_state_t s = BM_CRITICAL_ENTER();

    if (_modules_initialized != BM_MODULES_UNINITIALIZED) {
        BM_CRITICAL_EXIT(s);
        BM_LOGW("module", "init_all_for_domain already done");
        return BM_ERR_ALREADY;
    }
    _modules_initialized = BM_MODULES_INITIALIZING;
    BM_CRITICAL_EXIT(s);

    /*
     * 先校验模块表上界，通过后再 reset 事件总线。
     * 避免校验失败时已 reset 事件总线导致已发布事件不可逆丢失。
     */
    if (_bm_module_count > BM_CONFIG_MAX_MODULES) {
        BM_LOGE("module", "module table truncated: %u > %u",
                (unsigned)_bm_module_count, (unsigned)BM_CONFIG_MAX_MODULES);
        s = BM_CRITICAL_ENTER();
        _modules_initialized = BM_MODULES_UNINITIALIZED;
        BM_CRITICAL_EXIT(s);
        return BM_ERR_OVERFLOW;
    }

    if (reset_event_bus) {
        bm_event_reset();
    }

    _module_count = 0;
    for (uint32_t i = 0u; i < _bm_module_count; i++) {
        if (_bm_module_table[i] == NULL) {
            BM_LOGE("module", "module table contains null entry idx=%u",
                    (unsigned)i);
            s = BM_CRITICAL_ENTER();
            _module_count = 0u;
            _modules_initialized = BM_MODULES_UNINITIALIZED;
            BM_CRITICAL_EXIT(s);
            return BM_ERR_INVALID;
        }
        if (_bm_module_table[i]->domain == domain ||
            _bm_module_table[i]->domain == BM_DOMAIN_COMMON) {
            memcpy(&_modules[_module_count], _bm_module_table[i], sizeof(bm_module_t));
            _modules[_module_count].state = BM_MODULE_STATE_UNINIT;
            _module_count++;
        }
    }

    _sort_modules();

    BM_LOGI("module", "init_all_for_domain count=%u", (unsigned)_module_count);
    for (uint32_t i = 0u; i < _module_count; i++) {
        int r = _modules[i].init ? _modules[i].init() : BM_OK;

        if (r == BM_OK) {
            _modules[i].state = BM_MODULE_STATE_INITED;
            BM_LOGD("module", "'%s' inited",
                    _modules[i].name ? _modules[i].name : "(null)");
        } else {
            int rollback_rc;

            BM_LOGE("module", "'%s' init failed rc=%d",
                    _modules[i].name ? _modules[i].name : "(null)", r);
            rollback_rc = _rollback_inits(i);
            if (rollback_rc != BM_OK) {
                s = BM_CRITICAL_ENTER();
                _modules_initialized = BM_MODULES_CLEANUP_PENDING;
                BM_CRITICAL_EXIT(s);
            } else {
                s = BM_CRITICAL_ENTER();
                _module_count = 0u;
                _modules_initialized = BM_MODULES_UNINITIALIZED;
                BM_CRITICAL_EXIT(s);
            }
            return r;
        }
    }
    s = BM_CRITICAL_ENTER();
    _modules_initialized = BM_MODULES_READY;
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
    /*
     * _modules 中仅包含 init_all_for_domain(domain) 加载的匹配模块；
     * 此处直接调用 start_all 仅操作已过滤的模块集。
     */
    (void)domain;
    return bm_module_start_all();
}

/**
 * @brief 停止匹配指定 domain 的已启动模块
 *
 * @param domain 目标域
 * @return BM_OK 成功；负值表示失败
 */
int bm_module_stop_all_for_domain(bm_domain_t domain) {
    /*
     * 同理，stop_all 逆序遍历 _modules，仅停止当前已加载的
     * 域匹配模块。
     */
    (void)domain;
    return bm_module_stop_all();
}

/**
 * @brief 反初始化匹配指定 domain 的模块
 *
 * @param domain 目标域
 * @return BM_OK 成功；负值表示失败
 */
int bm_module_deinit_all_for_domain(bm_domain_t domain) {
    (void)domain;
    return bm_module_deinit_all();
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
    bm_irq_state_t s;
    uint32_t i;

    if (module_require_cpu() != BM_OK) {
        return BM_ERR_INVALID;
    }
    if (!s_owner_resolver) {
        return BM_ERR_NOT_INIT;
    }

    s = BM_CRITICAL_ENTER();
    if (_modules_initialized != BM_MODULES_UNINITIALIZED) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_ALREADY;
    }
    _modules_initialized = BM_MODULES_INITIALIZING;
    BM_CRITICAL_EXIT(s);

    if (_bm_module_count > BM_CONFIG_MAX_MODULES) {
        s = BM_CRITICAL_ENTER();
        _modules_initialized = BM_MODULES_UNINITIALIZED;
        BM_CRITICAL_EXIT(s);
        return BM_ERR_OVERFLOW;
    }

    _module_count = 0u;
    for (i = 0u; i < _bm_module_count; i++) {
        if (_bm_module_table[i] == NULL) {
            s = BM_CRITICAL_ENTER();
            _modules_initialized = BM_MODULES_UNINITIALIZED;
            BM_CRITICAL_EXIT(s);
            return BM_ERR_INVALID;
        }
        {
            uint8_t owner = s_owner_resolver(i);
            /* BM_CPU_ANY 表示“在所有 CPU 上运行”；每个 CPU 均应包含该模块。 */
            if (owner != BM_CPU_ANY &&
                owner != (uint8_t)bm_hal_cpu_id()) {
                continue;
            }
        }
        memcpy(&_modules[_module_count], _bm_module_table[i], sizeof(bm_module_t));
        _modules[_module_count].state = BM_MODULE_STATE_UNINIT;
        _module_count++;
    }

    _sort_modules();
    BM_LOGI("module", "init_on_this_cpu count=%u", (unsigned)_module_count);
    for (i = 0u; i < _module_count; i++) {
        int r = _modules[i].init ? _modules[i].init() : BM_OK;

        if (r != BM_OK) {
            (void)_rollback_inits(i);
            s = BM_CRITICAL_ENTER();
            _module_count = 0u;
            _modules_initialized = BM_MODULES_UNINITIALIZED;
            BM_CRITICAL_EXIT(s);
            return r;
        }
        _modules[i].state = BM_MODULE_STATE_INITED;
    }
    s = BM_CRITICAL_ENTER();
    _modules_initialized = BM_MODULES_READY;
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
    bm_irq_state_t s;
    uint32_t i;

    if (module_require_cpu() != BM_OK) {
        return BM_ERR_INVALID;
    }

    s = BM_CRITICAL_ENTER();
    if (_modules_initialized != BM_MODULES_READY) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_NOT_INIT;
    }
    _modules_initialized = BM_MODULES_TRANSITIONING;
    BM_CRITICAL_EXIT(s);

    for (i = 0u; i < _module_count; i++) {
        if (_modules[i].state == BM_MODULE_STATE_INITED ||
            _modules[i].state == BM_MODULE_STATE_STOPPED) {
            int r = _modules[i].start ? _modules[i].start() : BM_OK;

            if (r != BM_OK) {
                (void)_rollback_starts(i);
                s = BM_CRITICAL_ENTER();
                _modules_initialized = BM_MODULES_READY;
                BM_CRITICAL_EXIT(s);
                return r;
            }
            _modules[i].state = BM_MODULE_STATE_STARTED;
        }
    }
    s = BM_CRITICAL_ENTER();
    _modules_initialized = BM_MODULES_READY;
    BM_CRITICAL_EXIT(s);
    return BM_OK;
#endif
}
