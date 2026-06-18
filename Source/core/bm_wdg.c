/**
 * @file bm_wdg.c
 * @brief 软件看门狗模块注册与喂狗实现
 *
 * 所有已注册模块均按时喂狗后才向硬件 WDG 提交喂狗信号。
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-10       1.1            zeh            模块超时窗口与空指针防护
 * 2026-06-11       1.2            zeh            独立 fed 标志、临界区、防重名注册
 * 2026-06-15       1.3            zeh            运行期喂狗后冻结注册表
 *
 */
#include "bm_wdg.h"
#include "bm_critical_wrap.h"
#include "bm_hal_timer.h"
#include "bm_hal_wdg.h"
#include "bm_log.h"
#include "bm_time.h"
#include "bm/core/bm_cpu_local.h"
/*
 * 按 CPU 路由启用时的使用条件（Bootstrap CPU 约束）：
 *
 * bm_wdg 软件看门狗模块注册表 (_wdg_modules) 为静态全局数据。
 * 仅 Bootstrap CPU 调用 bm_wdg_register / bm_wdg_feed_module /
 * bm_wdg_feed。Bootstrap 在 bm_wdg_feed() 内通过放行钩子聚合检查。
 */
#include "hal/bm_hal_cpu.h"

#include <stdbool.h>
#include <string.h>

#ifndef BM_CONFIG_WDG_MODULE_TIMEOUT_MS
#define BM_CONFIG_WDG_MODULE_TIMEOUT_MS 1000
#endif

/** 已注册模块记录 */
typedef struct {
    char        name[BM_CONFIG_WDG_MAX_NAME_LEN];
    uint32_t    last_feed_ticks;
    bool        fed;
} bm_wdg_module_t;

static bm_wdg_module_t _wdg_modules[BM_CONFIG_MAX_WDG_MODULES];
static uint32_t        _wdg_module_count = 0;
static uint32_t        _wdg_generation = 0;
static bool            _wdg_registry_frozen = false;

static int (*s_gate_hook)(void);

void bm_wdg_set_gate_hook(int (*hook)(void)) {
    s_gate_hook = hook;
}

/**
 * @brief 计算看门狗模块名称有效长度
 *
 * @param name 名称字符串
 * @param length_out 输出长度
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效或长度越界
 */
static int wdg_name_length(const char *name, size_t *length_out) {
    size_t length = 0u;

    if (!name || !length_out) {
        return BM_ERR_INVALID;
    }
    while (length < BM_CONFIG_WDG_MAX_NAME_LEN && name[length] != '\0') {
        length++;
    }
    if (length == 0u || length >= BM_CONFIG_WDG_MAX_NAME_LEN) {
        return BM_ERR_INVALID;
    }
    *length_out = length;
    return BM_OK;
}

/**
 * @brief 将毫秒超时转换为定时器 tick（溢出安全）
 */
/**
 * @brief 将毫秒超时转换为定时器 tick（溢出安全）
 *
 * @param ticks_out 输出 tick 数
 * @return BM_OK 成功；负值表示失败
 */
static int wdg_timeout_ticks(uint32_t *ticks_out) {
    int rc = bm_time_ms_to_ticks(BM_CONFIG_WDG_MODULE_TIMEOUT_MS, ticks_out);

    if (rc == BM_ERR_INVALID) {
        return BM_ERR_NOT_INIT;
    }
    if (rc != BM_OK) {
        return rc;
    }
    if (*ticks_out == 0u) {
        *ticks_out = 1u;
    }
    return BM_OK;
}

/**
 * @brief 判断模块在本周期内是否已按时喂狗
 */
/**
 * @brief 判断模块在本周期内是否已按时喂狗
 *
 * @param mod 模块记录指针
 * @param now 当前 tick
 * @param timeout_ticks 超时 tick 数
 * @return 非零表示新鲜；0 表示已超时或未喂
 */
static int wdg_module_fresh(const bm_wdg_module_t *mod, uint32_t now,
                            uint32_t timeout_ticks) {
    uint32_t elapsed;

    if (!mod->fed) {
        return 0;
    }
    elapsed = now - mod->last_feed_ticks;
    return elapsed <= timeout_ticks;
}

/**
 * @brief 注册一个需喂狗的软件模块
 *
 * @param name 模块名称字符串
 * @return BM_OK 成功；BM_ERR_NO_MEM 注册表已满；BM_ERR_INVALID 参数无效；
 *         BM_ERR_ALREADY 同名模块已注册
 */
int bm_wdg_register(const char *name) {
    char registered_names[BM_CONFIG_MAX_WDG_MODULES]
                         [BM_CONFIG_WDG_MAX_NAME_LEN];
    size_t name_len;
    uint32_t count;
    uint32_t generation;
    uint32_t i;
    bm_irq_state_t s;

#if BM_CPU_LOCAL_ENABLE_ROUTE
    if (!bm_hal_cpu_is_bootstrap()) {
        return BM_ERR_INVALID;
    }
#endif
    if (wdg_name_length(name, &name_len) != BM_OK) {
        return BM_ERR_INVALID;
    }

    s = BM_CRITICAL_ENTER();
    count = _wdg_module_count;
    generation = _wdg_generation;
    if (_wdg_registry_frozen) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_BUSY;
    }
    if (count > BM_CONFIG_MAX_WDG_MODULES) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_INVALID;
    }
    for (i = 0u; i < count; i++) {
        memcpy(registered_names[i], _wdg_modules[i].name,
               BM_CONFIG_WDG_MAX_NAME_LEN);
    }
    BM_CRITICAL_EXIT(s);

    for (i = 0u; i < count; i++) {
        if (strcmp(registered_names[i], name) == 0) {
            BM_LOGW("wdg", "module '%s' already registered", name);
            return BM_ERR_ALREADY;
        }
    }

    s = BM_CRITICAL_ENTER();
    if (_wdg_registry_frozen ||
        _wdg_generation != generation || _wdg_module_count != count) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_BUSY;
    }
    if (count >= BM_CONFIG_MAX_WDG_MODULES) {
        BM_CRITICAL_EXIT(s);
        BM_LOGW("wdg", "register table full");
        return BM_ERR_NO_MEM;
    }
    memcpy(_wdg_modules[count].name, name, name_len + 1u);
    _wdg_modules[count].last_feed_ticks = 0u;
    _wdg_modules[count].fed = false;
    _wdg_module_count = count + 1u;
    _wdg_generation++;
    BM_CRITICAL_EXIT(s);

    BM_LOGI("wdg", "module '%s' registered", name);
    return BM_OK;
}

/**
 * @brief 记录指定模块的喂狗时间戳
 *
 * @param name 模块名称字符串
 */
void bm_wdg_feed_module(const char *name) {
    char registered_names[BM_CONFIG_MAX_WDG_MODULES]
                         [BM_CONFIG_WDG_MAX_NAME_LEN];
    size_t name_len;
    uint32_t count;
    uint32_t generation;
    uint32_t found = BM_CONFIG_MAX_WDG_MODULES;
    uint32_t i;
    uint32_t now;
    bm_irq_state_t s;

#if BM_CPU_LOCAL_ENABLE_ROUTE
    if (!bm_hal_cpu_is_bootstrap()) {
        BM_LOGW("wdg", "feed_module rejected on non-bootstrap cpu");
        return;
    }
#endif
    if (wdg_name_length(name, &name_len) != BM_OK) {
        BM_LOGW("wdg", "feed_module invalid name");
        return;
    }
    (void)name_len;

    s = BM_CRITICAL_ENTER();
    count = _wdg_module_count;
    generation = _wdg_generation;
    if (count > BM_CONFIG_MAX_WDG_MODULES) {
        BM_CRITICAL_EXIT(s);
        BM_LOGW("wdg", "feed module table corrupt");
        return;
    }
    for (i = 0u; i < count; i++) {
        memcpy(registered_names[i], _wdg_modules[i].name,
               BM_CONFIG_WDG_MAX_NAME_LEN);
    }
    BM_CRITICAL_EXIT(s);

    for (i = 0u; i < count; i++) {
        if (strcmp(registered_names[i], name) == 0) {
            found = i;
            break;
        }
    }
    if (found == BM_CONFIG_MAX_WDG_MODULES) {
        BM_LOGW("wdg", "feed unknown module '%s'", name);
        return;
    }

    now = bm_hal_timer_get_ticks();
    s = BM_CRITICAL_ENTER();
    if (_wdg_generation != generation || _wdg_module_count != count ||
        strcmp(_wdg_modules[found].name, registered_names[found]) != 0) {
        BM_CRITICAL_EXIT(s);
        BM_LOGW("wdg", "feed module table changed");
        return;
    }
    _wdg_modules[found].last_feed_ticks = now;
    _wdg_modules[found].fed = true;
    _wdg_generation++;
    BM_CRITICAL_EXIT(s);
    BM_LOGT("wdg", "feed module '%s'", name);
}

/**
 * @brief 检查所有模块均已按时喂狗后向硬件看门狗提交喂狗信号
 */
void bm_wdg_feed(void) {
    bm_wdg_module_t snapshot[BM_CONFIG_MAX_WDG_MODULES];
    uint32_t timeout_ticks = 0u;
    uint32_t count;
    uint32_t generation;
    int rc;
    uint32_t now;
    uint32_t i;
    bm_irq_state_t s;

#if BM_CPU_LOCAL_ENABLE_ROUTE
    if (!bm_hal_cpu_is_bootstrap()) {
        BM_LOGW("wdg", "hardware feed rejected on non-bootstrap cpu");
        return;
    }
#endif
    /*
     * 确定性流式安全：在临界区内同时捕获快照和当前 tick，
     * 确保乐观检查（快照超时）中 now 与 last_feed_ticks 时间一致。
     * 避免 ISR 在快照之后、now 之前喂狗导致的假超时→假复位。
     */
    s = BM_CRITICAL_ENTER();
    count = _wdg_module_count;
    generation = _wdg_generation;
    _wdg_registry_frozen = true;
    if (count > BM_CONFIG_MAX_WDG_MODULES) {
        BM_CRITICAL_EXIT(s);
        BM_LOGD("wdg", "hw feed blocked: module table corrupt");
        return;
    }
    now = bm_hal_timer_get_ticks();
    for (i = 0u; i < count; i++) {
        snapshot[i] = _wdg_modules[i];
    }
    BM_CRITICAL_EXIT(s);

    if (s_gate_hook && s_gate_hook() != BM_OK) {
        BM_LOGD("wdg", "hw feed gated by aggregator hook");
        return;
    }

    if (count == 0u) {
        s = bm_hal_critical_enter();
        if (_wdg_generation != generation || _wdg_module_count != 0u) {
            bm_hal_critical_exit(s);
            BM_LOGD("wdg", "hw feed blocked: module table changed");
            return;
        }
        bm_hal_wdg_feed();
        bm_hal_critical_exit(s);
        BM_LOGT("wdg", "hw feed (no modules)");
        return;
    }

    rc = wdg_timeout_ticks(&timeout_ticks);
    if (rc != BM_OK) {
        BM_LOGD("wdg", "hw feed blocked: timer not ready rc=%d", rc);
        return;
    }

    for (i = 0u; i < count; i++) {
        if (!wdg_module_fresh(&snapshot[i], now, timeout_ticks)) {
            BM_LOGD("wdg", "hw feed blocked: '%s' stale",
                    snapshot[i].name);
            return;
        }
    }

    /* 最终复核与硬件提交使用全 IRQ 屏蔽，避免优先级掩码允许 HRT 抢占。 */
    s = bm_hal_critical_enter();
    if (_wdg_generation != generation || _wdg_module_count != count) {
        bm_hal_critical_exit(s);
        BM_LOGD("wdg", "hw feed blocked: module state changed");
        return;
    }
    now = bm_hal_timer_get_ticks();
    for (i = 0u; i < count; i++) {
        if (!wdg_module_fresh(&_wdg_modules[i], now, timeout_ticks)) {
            bm_hal_critical_exit(s);
            BM_LOGD("wdg", "hw feed blocked: module became stale");
            return;
        }
    }
    for (i = 0u; i < count; i++) {
        _wdg_modules[i].fed = false;
        _wdg_modules[i].last_feed_ticks = 0u;
    }
    _wdg_generation++;
    bm_hal_wdg_feed();
    bm_hal_critical_exit(s);
    BM_LOGT("wdg", "hw feed all modules ok");
}

/**
 * @brief 清空软件看门狗注册表（仅用于测试或停机）
 */
void bm_wdg_reset(void) {
#if BM_CPU_LOCAL_ENABLE_ROUTE
    if (!bm_hal_cpu_is_bootstrap()) {
        return;
    }
#endif
    bm_irq_state_t s = BM_CRITICAL_ENTER();
    memset(_wdg_modules, 0, sizeof(_wdg_modules));
    _wdg_module_count = 0u;
    _wdg_registry_frozen = false;
    _wdg_generation++;
    BM_CRITICAL_EXIT(s);
}
