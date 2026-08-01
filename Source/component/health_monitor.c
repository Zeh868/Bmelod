/**
 * @file health_monitor.c
 * @brief 系统健康聚合组件实现
 *
 * 事件驱动：bm_health_monitor_report() 更新故障源表项并重算系统级
 * 健康快照；快照较上一次发布有变化时发布一次遥测。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       0.1            zeh            初始版本
 * 2026-08-01       0.1            Codex           补全 Doxygen 合规注释
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/component/health_monitor.h"
#include "bm/common/bm_types.h"
#include "bm/component/bm_component_common.h"

#include <string.h>

/**
 * @brief 按故障源编号查找健康监视源
 * @param mon 健康监视器实例
 * @param source_id 故障源编号
 * @return 匹配的故障源指针；未找到时返回 NULL
 */
static bm_health_monitor_source_t *find_source(bm_health_monitor_t *mon,
                                               uint16_t source_id) {
    uint32_t i;

    for (i = 0u; i < mon->config.source_count; i++) {
        if (mon->config.sources[i].source_id == source_id) {
            return &mon->config.sources[i];
        }
    }
    return NULL;
}

/**
 * @brief 由故障源表重新计算系统级健康快照
 * @param mon 健康监视器实例
 * @param snap 待更新的遥测快照；sequence 由调用方填写
 */
static void recompute_snapshot(const bm_health_monitor_t *mon,
                               bm_health_monitor_telemetry_t *snap) {
    uint32_t i;

    memset(snap, 0, sizeof(*snap));
    snap->status = BM_HEALTH_MONITOR_TEL_VALID;
    snap->worst_severity = (uint8_t)BM_FAULT_SEVERITY_NONE;
    snap->active_code = BM_FAULT_NONE;

    for (i = 0u; i < mon->config.source_count; i++) {
        const bm_health_monitor_source_t *src = &mon->config.sources[i];

        if (src->latched_code != BM_FAULT_NONE) {
            snap->sources_latched++;
        }
        if (src->active_code == BM_FAULT_NONE) {
            continue;
        }
        snap->sources_active++;
        /* 严重度数值越大越严重；并列时保留先出现的源（表序即优先级） */
        if (src->severity > snap->worst_severity) {
            snap->worst_severity = src->severity;
            snap->active_source_id = src->source_id;
            snap->active_code = src->active_code;
        }
    }
}

/**
 * @brief 比较两份健康监视遥测快照是否具有相同内容
 * @param a 第一份遥测快照
 * @param b 第二份遥测快照
 * @return 内容相同返回 1，否则返回 0
 */
static int snapshot_equal(const bm_health_monitor_telemetry_t *a,
                          const bm_health_monitor_telemetry_t *b) {
    return a->status == b->status &&
           a->worst_severity == b->worst_severity &&
           a->sources_active == b->sources_active &&
           a->sources_latched == b->sources_latched &&
           a->active_source_id == b->active_source_id &&
           a->active_code == b->active_code;
}

int bm_health_monitor_validate_config(const bm_health_monitor_config_t *config) {
    if (config == NULL || config->sources == NULL ||
        config->source_count == 0u) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

void bm_health_monitor_reset(bm_health_monitor_t *mon) {
    uint32_t i;

    if (mon == NULL) {
        return;
    }

    for (i = 0u; i < mon->config.source_count; i++) {
        bm_health_monitor_source_t *src = &mon->config.sources[i];

        src->active_code = BM_FAULT_NONE;
        src->latched_code = BM_FAULT_NONE;
        src->severity = (uint8_t)BM_FAULT_SEVERITY_NONE;
        src->worst_severity = (uint8_t)BM_FAULT_SEVERITY_NONE;
        src->report_count = 0u;
    }
    mon->state.report_seq = 0u;
    memset(&mon->state.telemetry, 0, sizeof(mon->state.telemetry));
}

int bm_health_monitor_init(bm_health_monitor_t *mon) {
    if (mon == NULL ||
        bm_health_monitor_validate_config(&mon->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_health_monitor_reset(mon);
    return BM_OK;
}

int bm_health_monitor_report(bm_health_monitor_t *mon,
                             uint16_t source_id,
                             bm_fault_code_t code,
                             bm_fault_severity_t severity) {
    bm_health_monitor_source_t *src;
    bm_health_monitor_telemetry_t snap;

    if (mon == NULL) {
        return BM_ERR_INVALID;
    }

    src = find_source(mon, source_id);
    if (src == NULL) {
        return BM_ERR_INVALID;
    }

    if (code == BM_FAULT_NONE) {
        src->active_code = BM_FAULT_NONE;
        src->severity = (uint8_t)BM_FAULT_SEVERITY_NONE;
    } else {
        src->active_code = code;
        src->severity = (uint8_t)severity;
        src->latched_code = code;
        if ((uint8_t)severity > src->worst_severity) {
            src->worst_severity = (uint8_t)severity;
        }
    }
    src->report_count++;

    recompute_snapshot(mon, &snap);
    if (snapshot_equal(&snap, &mon->state.telemetry)) {
        return BM_OK;
    }

    mon->state.report_seq++;
    snap.sequence = mon->state.report_seq;
    mon->state.telemetry = snap;
    BM_COMPONENT_PUBLISH_TELEMETRY(mon, &mon->state.telemetry);
    return BM_OK;
}

int bm_health_monitor_system_health(const bm_health_monitor_t *mon,
                                    bm_health_monitor_telemetry_t *out) {
    if (mon == NULL || out == NULL) {
        return BM_ERR_INVALID;
    }
    *out = mon->state.telemetry;
    return BM_OK;
}
