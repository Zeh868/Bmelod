/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_wdg.c
 * @brief 软件看门狗模块注册与喂狗实现
 *
 * 所有已注册模块均按时喂狗后才向硬件 WDG 提交喂狗信号。
 * @author zeh (china_qzh@163.com)
 * @version 1.5
 * @date 2026-06-29
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-10       1.1            zeh            模块超时窗口与空指针防护
 * 2026-06-11       1.2            zeh            独立 fed 标志、临界区、防重名注册
 * 2026-06-15       1.3            zeh            运行期喂狗后冻结注册表
 * 2026-06-26       1.4            zeh            时间基迁至 bm_uptime_us()（#9-2a）
 * 2026-06-29       1.5            zeh            register/feed_module 收敛为单段临界区，消除栈上整表拷贝
 *
 */
#include "bm_wdg.h"
#include "bm_critical_wrap.h"
#include "bm_hal_wdg.h"
#include "bm_log.h"
#include "bm/common/bm_uptime.h"
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
    uint64_t    last_feed_us;   /**< 最近一次喂狗的 bm_uptime_us() 值（微秒） */
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
 * @brief 计算看门狗模块超时微秒数（#9-2a：不依赖硬件定时器）
 *
 * 直接将配置的毫秒超时换算为微秒，不再调用 bm_hal_timer_get_freq()。
 *
 * @param us_out 输出超时微秒数
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效
 */
static int wdg_timeout_us(uint64_t *us_out) {
    if (us_out == NULL) {
        return BM_ERR_INVALID;
    }
    *us_out = (BM_CONFIG_WDG_MODULE_TIMEOUT_MS == 0u)
              ? 1u
              : (uint64_t)BM_CONFIG_WDG_MODULE_TIMEOUT_MS * 1000u;
    return BM_OK;
}

/**
 * @brief 判断模块在本周期内是否已按时喂狗（#9-2a：64 位 uptime 微秒）
 *
 * @param mod        模块记录指针
 * @param now_us     当前 bm_uptime_us() 值（微秒）
 * @param timeout_us 超时微秒数
 * @return 非零表示新鲜（已喂且未超时）；0 表示未喂或已超时
 */
static int wdg_module_fresh(const bm_wdg_module_t *mod, uint64_t now_us,
                            uint64_t timeout_us) {
    uint64_t elapsed;

    if (!mod->fed) {
        return 0;
    }
    elapsed = now_us - mod->last_feed_us;
    return elapsed <= timeout_us;
}

/**
 * @brief 注册一个需喂狗的软件模块
 *
 * 在**单段临界区**内完成重名扫描与插入，日志在出锁后根据结果码发出。
 * 消除了原三段式（锁内拷整表→锁外 strcmp→再锁内 generation 复核）带来的
 * 栈上 ~512 B 整表拷贝与 TOCTOU 舞步；单锁内读写一致，O(n) 有界扫描
 * （n ≤ BM_CONFIG_MAX_WDG_MODULES）。
 * 临界区内禁止 BM_LOGx，以整型 result 记录结果，出锁后再输出日志。
 *
 * @param name 模块名称字符串
 * @return BM_OK 成功；BM_ERR_NO_MEM 注册表已满；BM_ERR_INVALID 参数无效；
 *         BM_ERR_ALREADY 同名模块已注册；BM_ERR_BUSY 注册表已冻结
 */
int bm_wdg_register(const char *name) {
    size_t name_len;
    uint32_t count;
    uint32_t i;
    bm_irq_state_t s;
    int result;

#if BM_CPU_LOCAL_ENABLE_ROUTE
    if (!bm_hal_cpu_is_bootstrap()) {
        return BM_ERR_INVALID;
    }
#endif
    if (wdg_name_length(name, &name_len) != BM_OK) {
        return BM_ERR_INVALID;
    }

    /* 单段临界区：扫描重名 + 插入，临界区内不调用 BM_LOGx */
    s = BM_CRITICAL_ENTER();
    if (_wdg_registry_frozen) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_BUSY;
    }
    count = _wdg_module_count;
    if (count > BM_CONFIG_MAX_WDG_MODULES) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_INVALID;
    }
    result = BM_OK;
    for (i = 0u; i < count; i++) {
        if (strcmp(_wdg_modules[i].name, name) == 0) {
            result = BM_ERR_ALREADY;
            break;
        }
    }
    if (result == BM_OK) {
        if (count >= BM_CONFIG_MAX_WDG_MODULES) {
            result = BM_ERR_NO_MEM;
        } else {
            memcpy(_wdg_modules[count].name, name, name_len + 1u);
            _wdg_modules[count].last_feed_us = 0u;
            _wdg_modules[count].fed = false;
            _wdg_module_count = count + 1u;
            _wdg_generation++;
        }
    }
    BM_CRITICAL_EXIT(s);

    /* 锁外输出日志，文案与返回码与原版保持一致 */
    if (result == BM_ERR_ALREADY) {
        BM_LOGW("wdg", "module '%s' already registered", name);
    } else if (result == BM_ERR_NO_MEM) {
        BM_LOGW("wdg", "register table full");
    } else {
        BM_LOGI("wdg", "module '%s' registered", name);
    }
    return result;
}

/**
 * @brief 记录指定模块的喂狗时间戳
 *
 * 在**单段临界区**内直接对注册名表做有界 strcmp 扫描（≤ _wdg_module_count），
 * 找到后在同一临界区内更新 last_feed_us 与 fed 标志，日志移至锁外。
 * 消除了原两段式（锁内拷整表→锁外 strcmp→再锁内 generation 复核）的
 * 栈上 ~512 B 整表拷贝与 TOCTOU generation 自增舞步；
 * feed 路径不再 bump _wdg_generation（该自增仅服务于已删除的复核 dance，
 * 全库无其他依赖，bm_wdg_feed() 最终复核以 wdg_module_fresh() 为门控）。
 * 临界区内禁止 BM_LOGx，以 bool found 记录结果，出锁后再输出日志。
 *
 * @param name 模块名称字符串
 */
void bm_wdg_feed_module(const char *name) {
    size_t name_len;
    uint32_t count;
    uint32_t i;
    uint64_t now_us;
    bm_irq_state_t s;
    bool found;

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

    now_us = bm_uptime_us();  /* 锁外采样时间戳，避免临界区调用 syscall */

    /* 单段临界区：扫描 + 更新，临界区内不调用 BM_LOGx */
    s = BM_CRITICAL_ENTER();
    count = _wdg_module_count;
    if (count > BM_CONFIG_MAX_WDG_MODULES) {
        BM_CRITICAL_EXIT(s);
        BM_LOGW("wdg", "feed module table corrupt");
        return;
    }
    found = false;
    for (i = 0u; i < count; i++) {
        if (strcmp(_wdg_modules[i].name, name) == 0) {
            _wdg_modules[i].last_feed_us = now_us;
            _wdg_modules[i].fed = true;
            found = true;
            break;
        }
    }
    BM_CRITICAL_EXIT(s);

    /* 锁外输出日志，文案与原版保持一致 */
    if (!found) {
        BM_LOGW("wdg", "feed unknown module '%s'", name);
        return;
    }
    BM_LOGT("wdg", "feed module '%s'", name);
}

/**
 * @brief 检查所有模块均已按时喂狗后向硬件看门狗提交喂狗信号
 */
void bm_wdg_feed(void) {
    bm_wdg_module_t snapshot[BM_CONFIG_MAX_WDG_MODULES];
    uint64_t timeout_us = 0u;
    uint32_t count;
    uint32_t generation;
    int rc;
    uint64_t now_us;
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
    now_us = bm_uptime_us();
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

    rc = wdg_timeout_us(&timeout_us);
    if (rc != BM_OK) {
        BM_LOGD("wdg", "hw feed blocked: timeout compute failed rc=%d", rc);
        return;
    }

    for (i = 0u; i < count; i++) {
        if (!wdg_module_fresh(&snapshot[i], now_us, timeout_us)) {
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
    now_us = bm_uptime_us();
    for (i = 0u; i < count; i++) {
        if (!wdg_module_fresh(&_wdg_modules[i], now_us, timeout_us)) {
            bm_hal_critical_exit(s);
            BM_LOGD("wdg", "hw feed blocked: module became stale");
            return;
        }
    }
    for (i = 0u; i < count; i++) {
        _wdg_modules[i].fed = false;
        _wdg_modules[i].last_feed_us = 0u;
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
