/**
 * @file bm_mp_stream_gate.c
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @brief Stream schedule gate 实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 *
 */
#include "bm/mp/bm_mp_stream_gate.h"
#include "bm_log.h"

#include <string.h>

uint32_t bm_mp_stream_derive_scan_us(uint32_t block_count,
                                     uint32_t scan_us_per_block) {
    uint64_t scan_us;

    if (block_count == 0u || scan_us_per_block == 0u) {
        return 0u;
    }
    scan_us = (uint64_t)block_count * (uint64_t)scan_us_per_block;
    return scan_us <= UINT32_MAX ? (uint32_t)scan_us : 0u;
}

uint32_t bm_mp_stream_derive_min_depth(
    const bm_mp_stream_gate_params_t *params,
    bm_mp_stream_gate_report_t *report_out) {
    uint64_t max_arrivals;
    uint64_t min_depth;
    uint32_t scan_us;
    uint32_t commit_us;
    uint32_t cache_maint_us;
    uint64_t service_us;
    int sustainable = 0;

    if (!params || params->block_period_us == 0u) {
        return 0u;
    }

    max_arrivals = 1u;
    if (params->response_window_us > 0u) {
        max_arrivals =
            ((uint64_t)params->response_window_us +
             (uint64_t)params->block_period_us - 1u) /
            (uint64_t)params->block_period_us;
        if (max_arrivals == 0u) {
            max_arrivals = 1u;
        }
    }
    min_depth = max_arrivals + params->burst_blocks + 1u;
    if (min_depth < 2u) {
        min_depth = 2u;
    }

    scan_us = bm_mp_stream_derive_scan_us(
        params->block_count,
        params->scan_us_per_block ?
            params->scan_us_per_block :
            BM_CONFIG_MP_STREAM_SCAN_US_PER_BLOCK);
    if (scan_us == 0u || min_depth > UINT32_MAX) {
        return 0u;
    }
    commit_us = params->commit_us ?
                    params->commit_us :
                    BM_CONFIG_MP_STREAM_COMMIT_US_DEFAULT;
    cache_maint_us = params->cache_maint_us ?
                         params->cache_maint_us :
                         BM_CONFIG_MP_STREAM_CACHE_MAINT_US_DEFAULT;
    service_us = (uint64_t)params->processing_wcet_us +
                 (uint64_t)scan_us +
                 (uint64_t)commit_us +
                 (uint64_t)cache_maint_us;
    sustainable = (service_us < params->block_period_us) ? 1 : 0;

    if (report_out) {
        memset(report_out, 0, sizeof(*report_out));
        report_out->min_queue_depth = (uint32_t)min_depth;
        report_out->derived_scan_us = scan_us;
        report_out->service_sustainable = sustainable;
        if (params->block_count > 1u) {
            uint64_t delay =
                (uint64_t)(params->block_count - 1u) *
                (uint64_t)params->block_period_us;
            if (delay > UINT32_MAX) {
                return 0u;
            }
            report_out->max_queue_delay_us = (uint32_t)delay;
        }
    }

    return sustainable ? (uint32_t)min_depth : 0u;
}

int bm_mp_stream_validate_gate(const bm_mp_stream_gate_params_t *params,
                               bm_mp_stream_gate_report_t *report_out) {
    bm_mp_stream_gate_report_t local;
    bm_mp_stream_gate_report_t *rep = report_out ? report_out : &local;
    uint32_t min_depth;
    uint32_t commit_us;
    uint32_t cache_maint_us;
    uint64_t service_us;

    if (!params || params->block_count < 2u ||
        params->block_period_us == 0u || params->block_deadline_us == 0u) {
        return BM_ERR_INVALID;
    }

    min_depth = bm_mp_stream_derive_min_depth(params, rep);
    if (min_depth == 0u || !rep->service_sustainable) {
        commit_us = params->commit_us ?
                        params->commit_us :
                        BM_CONFIG_MP_STREAM_COMMIT_US_DEFAULT;
        cache_maint_us = params->cache_maint_us ?
                             params->cache_maint_us :
                             BM_CONFIG_MP_STREAM_CACHE_MAINT_US_DEFAULT;
        service_us = (uint64_t)params->processing_wcet_us +
                     (uint64_t)rep->derived_scan_us +
                     (uint64_t)commit_us +
                     (uint64_t)cache_maint_us;
        BM_LOGE("mp_gate", "unsustainable service total=%u period=%u",
                (unsigned)(service_us > UINT32_MAX ?
                               UINT32_MAX : service_us),
                (unsigned)params->block_period_us);
        return BM_ERR_INVALID;
    }
    if (params->block_count < min_depth) {
        BM_LOGE("mp_gate", "depth %u < min %u",
                (unsigned)params->block_count, (unsigned)min_depth);
        return BM_ERR_INVALID;
    }
    commit_us = params->commit_us ?
                    params->commit_us :
                    BM_CONFIG_MP_STREAM_COMMIT_US_DEFAULT;
    cache_maint_us = params->cache_maint_us ?
                         params->cache_maint_us :
                         BM_CONFIG_MP_STREAM_CACHE_MAINT_US_DEFAULT;
    service_us = (uint64_t)params->processing_wcet_us +
                 (uint64_t)rep->derived_scan_us +
                 (uint64_t)commit_us +
                 (uint64_t)cache_maint_us;
    if ((uint64_t)rep->max_queue_delay_us + service_us >
        (uint64_t)params->block_deadline_us) {
        BM_LOGE("mp_gate", "queue delay %u + service > deadline %u",
                (unsigned)rep->max_queue_delay_us,
                (unsigned)params->block_deadline_us);
        return BM_ERR_OVERFLOW;
    }
    return BM_OK;
}

int bm_mp_schedule_apply_stream_gate(bm_mp_schedule_slot_t *slot,
                                     const bm_mp_stream_gate_params_t *params) {
    bm_mp_stream_gate_report_t report;
    uint32_t scan_us;

    if (!slot || !params) {
        return BM_ERR_INVALID;
    }
    if ((slot->flags & BM_MP_SCHEDULE_FLAG_STREAM) == 0u) {
        return BM_ERR_INVALID;
    }
    {
        int rc = bm_mp_stream_validate_gate(params, &report);
        if (rc != BM_OK) {
            return rc;
        }
    }

    scan_us = params->scan_us_per_block ?
                  bm_mp_stream_derive_scan_us(params->block_count,
                                              params->scan_us_per_block) :
                  report.derived_scan_us;
    slot->stream_scan_us = scan_us;
    slot->stream_commit_us = params->commit_us ?
                                 params->commit_us :
                                 BM_CONFIG_MP_STREAM_COMMIT_US_DEFAULT;
    slot->cache_maint_us = params->cache_maint_us ?
                               params->cache_maint_us :
                               BM_CONFIG_MP_STREAM_CACHE_MAINT_US_DEFAULT;
    return BM_OK;
}

int bm_mp_schedule_register_stream(
    const bm_mp_schedule_slot_t *slot,
    const bm_mp_stream_gate_params_t *gate) {
    bm_mp_schedule_slot_t copy;

    if (!slot || !gate) {
        return BM_ERR_INVALID;
    }
    copy = *slot;
    if (bm_mp_schedule_apply_stream_gate(&copy, gate) != BM_OK) {
        return BM_ERR_INVALID;
    }
    copy.flags &= ~BM_MP_SCHEDULE_FLAG_STREAM;  /* gate 已应用，清除标志 */
    return bm_mp_schedule_register(&copy);
}
