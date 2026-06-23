/**
 * @file daq_frontend.c
 * @brief DAQ 前端组件实现
 * @author zeh (china_qzh@163.com)
 * @version 0.3
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            补全 Doxygen 中文注释；添加 SPDX 头
 * 2026-06-23       0.3            zeh            修复预触发缓冲在触发后仍写入导致快照被污染
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "bm/component/daq_frontend.h"

#include <math.h>

/**
 * @brief 向预触发环形缓冲写入一个样本
 *
 * 采用覆盖式循环写：缓冲满时最旧样本被新样本覆盖，
 * 始终保留最近 pre_trigger_cap 个样本。
 * 若缓冲指针为 NULL 或容量为 0 则静默返回。
 *
 * @param axis   DAQ 轴实例指针
 * @param sample 待写入的采样值
 */
static void pre_trigger_push(bm_daq_frontend_axis_t *axis, float sample) {
    bm_daq_frontend_state_t *st = &axis->state;
    uint32_t cap;

    if (st->pre_trigger_buffer == NULL || st->pre_trigger_cap == 0u) {
        return;
    }

    cap = st->pre_trigger_cap;
    st->pre_trigger_buffer[st->pre_trigger_head] = sample;
    st->pre_trigger_head = (st->pre_trigger_head + 1u) % cap;
    if (st->pre_trigger_count < cap) {
        st->pre_trigger_count++;
    }
}

/**
 * @brief 初始化 DAQ 前端轴
 *
 * 绑定 RMS 滑窗缓冲区与预触发缓冲区，并调用 @ref bm_daq_frontend_reset
 * 将所有运行状态清零。配置中 pre_trigger_samples > 0 时必须提供有效的
 * pre_trigger_buffer；否则返回 BM_ERR_INVALID。
 *
 * @param axis                DAQ 轴实例指针，不可为 NULL
 * @param rms_buffer          RMS 滑窗缓冲区，不可为 NULL
 * @param rms_buflen          RMS 缓冲区长度（样本数），须 > 0
 * @param pre_trigger_buffer  预触发环形缓冲区；pre_trigger_samples == 0 时可为 NULL
 * @param pre_trigger_buflen  预触发缓冲区长度；pre_trigger_samples == 0 时可为 0
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法
 */
int bm_daq_frontend_init(bm_daq_frontend_axis_t *axis,
                         float *rms_buffer,
                         uint32_t rms_buflen,
                         float *pre_trigger_buffer,
                         uint32_t pre_trigger_buflen) {
    uint32_t cap;

    if (axis == NULL || rms_buffer == NULL || rms_buflen == 0u) {
        return BM_ERR_INVALID;
    }
    if (axis->config.pre_trigger_samples > 0u &&
        (pre_trigger_buffer == NULL || pre_trigger_buflen == 0u)) {
        return BM_ERR_INVALID;
    }

    axis->state.rms_buffer = rms_buffer;
    axis->state.rms_buflen = rms_buflen;
    axis->state.rms_cfg.window_samples = rms_buflen;
    if (bm_algo_rms_init(&axis->state.rms, &axis->state.rms_cfg,
                         rms_buffer, rms_buflen) != 0) {
        return BM_ERR_INVALID;
    }

    cap = pre_trigger_buflen;
    if (axis->config.pre_trigger_samples > 0u &&
        axis->config.pre_trigger_samples < cap) {
        cap = axis->config.pre_trigger_samples;
    }
    axis->state.pre_trigger_buffer = pre_trigger_buffer;
    axis->state.pre_trigger_cap = cap;

    bm_daq_frontend_reset(axis);
    return BM_OK;
}

/**
 * @brief 复位 DAQ 前端轴运行状态
 *
 * 清零 peak、crest_factor、captured、armed、triggered 及预触发游标，
 * 并重置 RMS 滑窗。缓冲区指针与容量保持不变。
 *
 * @param axis DAQ 轴实例指针，NULL 时静默返回
 */
void bm_daq_frontend_reset(bm_daq_frontend_axis_t *axis) {
    if (axis == NULL) {
        return;
    }
    bm_algo_rms_reset(&axis->state.rms);
    axis->state.pre_trigger_head = 0u;
    axis->state.pre_trigger_count = 0u;
    axis->state.peak = 0.0f;
    axis->state.crest_factor = 0.0f;
    axis->state.captured = 0u;
    axis->state.armed = 0;
    axis->state.triggered = 0;
}

/**
 * @brief 武装（arm）DAQ 前端，准备接受采样
 *
 * 置位 armed 标志，清零 triggered、captured、peak 及预触发游标。
 * 调用方须在 init 之后、feed 之前调用本函数；
 * 未调用 arm 直接 feed 将返回 BM_ERR_INVALID。
 *
 * @param axis DAQ 轴实例指针，NULL 时静默返回
 */
void bm_daq_frontend_arm(bm_daq_frontend_axis_t *axis) {
    if (axis != NULL) {
        axis->state.armed = 1;
        axis->state.triggered = 0;
        axis->state.captured = 0u;
        axis->state.pre_trigger_head = 0u;
        axis->state.pre_trigger_count = 0u;
        axis->state.peak = 0.0f;
    }
}

/**
 * @brief 推入一个采样值，驱动触发与采集逻辑
 *
 * 每次调用：
 * 1. 触发前将 sample 写入预触发环形缓冲（若已配置）；触发后冻结快照。
 * 2. 更新 RMS 滑窗与 peak 绝对值。
 * 3. 若 |sample| >= trigger_level 则置位 triggered。
 * 4. triggered 期间累计 captured 计数，并更新 crest_factor；
 *    captured 达到 capture_samples 时清零 armed 并返回 BM_DAQ_CAPTURE_DONE。
 *
 * @param axis   DAQ 轴实例指针，不可为 NULL
 * @param sample 当前采样值
 * @return BM_OK 继续采集；BM_DAQ_CAPTURE_DONE 后触发段采集完成；
 *         BM_ERR_INVALID 参数为 NULL 或轴未处于 armed 状态
 */
int bm_daq_frontend_feed(bm_daq_frontend_axis_t *axis, float sample) {
    float rms;
    float ax;

    if (axis == NULL) {
        return BM_ERR_INVALID;
    }
    if (!axis->state.armed) {
        return BM_ERR_INVALID;
    }

    rms = bm_algo_rms_step(&axis->state.rms, &axis->state.rms_cfg, sample);
    ax = fabsf(sample);
    if (ax > axis->state.peak) {
        axis->state.peak = ax;
    }

    if (!axis->state.triggered &&
        fabsf(sample) >= axis->config.trigger_level) {
        axis->state.triggered = 1;
    }

    /* 预触发缓冲仅保存触发前的样本：一旦触发即冻结，避免触发样本
     * 及之后的数据覆盖、污染触发前的波形快照。 */
    if (!axis->state.triggered) {
        pre_trigger_push(axis, sample);
    }

    if (axis->state.triggered) {
        axis->state.captured++;
        if (rms > 1e-9f) {
            axis->state.crest_factor = axis->state.peak / rms;
        }
        if (axis->state.captured >= axis->config.capture_samples) {
            axis->state.armed = 0;
            return BM_DAQ_CAPTURE_DONE;
        }
    }
    return BM_OK;
}

/**
 * @brief 按时间顺序复制预触发环形缓冲内容到目标数组
 *
 * dst[0] 为最旧样本，dst[n-1] 为最新样本。实际复制数取
 * min(pre_trigger_count, dst_len)。
 *
 * @param axis    DAQ 轴实例指针（const），NULL 时返回 0
 * @param dst     目标浮点数组，NULL 时返回 0
 * @param dst_len 目标数组可容纳的最大样本数，0 时返回 0
 * @return 实际复制的样本数；预触发缓冲为空或参数非法时返回 0
 */
uint32_t bm_daq_frontend_copy_pre_trigger(const bm_daq_frontend_axis_t *axis,
                                          float *dst,
                                          uint32_t dst_len) {
    const bm_daq_frontend_state_t *st;
    uint32_t cap;
    uint32_t n;
    uint32_t i;
    uint32_t start;

    if (axis == NULL || dst == NULL || dst_len == 0u) {
        return 0u;
    }

    st = &axis->state;
    if (st->pre_trigger_buffer == NULL || st->pre_trigger_cap == 0u ||
        st->pre_trigger_count == 0u) {
        return 0u;
    }

    cap = st->pre_trigger_cap;
    n = st->pre_trigger_count;
    if (n > dst_len) {
        n = dst_len;
    }
    if (n < cap) {
        for (i = 0u; i < n; i++) {
            dst[i] = st->pre_trigger_buffer[i];
        }
    } else {
        start = st->pre_trigger_head;
        for (i = 0u; i < n; i++) {
            dst[i] = st->pre_trigger_buffer[(start + i) % cap];
        }
    }
    return n;
}
