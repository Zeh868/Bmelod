/**
 * @file daq_frontend.h
 * @brief DAQ 前端：触发、预触发与 RMS/波峰因数捕获
 *
 * 提供单轴 DAQ 采样前端：arm/feed 流式接口驱动触发与后触发采集；
 * 内置 RMS 滑窗与预触发环形缓冲，适用于实时振动/电气信号诊断。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            补全 Doxygen 中文注释；添加 SPDX 头
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_DAQ_FRONTEND_H
#define BM_DAQ_FRONTEND_H

#include "bm/algorithm/bm_algo_power.h"
#include "bm/algorithm/bm_algo_statistics.h"
#include "bm/common/bm_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief bm_daq_frontend_feed 返回值：后触发段采集完成 */
#define BM_DAQ_CAPTURE_DONE  1

/**
 * @brief DAQ 前端轴配置
 */
typedef struct {
    float    trigger_level;          /**< 触发电平（绝对值），|sample| >= 该值时置位 triggered */
    uint32_t pre_trigger_samples;    /**< 预触发缓冲样本数；0 表示不使用预触发缓冲 */
    uint32_t capture_samples;        /**< 触发后后触发段最大采集样本数 */
    uint32_t sample_hz;              /**< 采样率（Hz），供上层时序计算参考 */
} bm_daq_frontend_config_t;

/**
 * @brief DAQ 前端轴运行状态
 *
 * 由 @ref bm_daq_frontend_init 绑定缓冲区，由 @ref bm_daq_frontend_reset 清零运行字段。
 * 用户不应直接修改此结构体成员。
 */
typedef struct {
    bm_algo_rms_config_t   rms_cfg;           /**< RMS 滑窗配置（由 init 填充） */
    bm_algo_rms_state_t    rms;               /**< RMS 滑窗运行状态 */
    float                 *rms_buffer;        /**< RMS 滑窗样本缓冲区 */
    uint32_t               rms_buflen;        /**< RMS 缓冲区长度（样本数） */
    float                 *pre_trigger_buffer; /**< 预触发环形缓冲区指针 */
    uint32_t               pre_trigger_cap;   /**< 预触发缓冲有效容量（样本数） */
    uint32_t               pre_trigger_head;  /**< 环形缓冲写指针（下一个写入位置） */
    uint32_t               pre_trigger_count; /**< 已写入的有效样本数（上限为 pre_trigger_cap） */
    float                  peak;              /**< 已见最大绝对值（自 arm 后重置） */
    float                  crest_factor;      /**< 当前波峰因数（peak / RMS） */
    uint32_t               captured;          /**< 后触发段已采集样本计数 */
    int                    armed;             /**< 非零表示已武装，等待触发或采集中 */
    int                    triggered;         /**< 非零表示已触发，后触发段计数中 */
} bm_daq_frontend_state_t;

/**
 * @brief DAQ 前端轴聚合对象
 *
 * 用户在使用前应填写 config 字段，然后调用 @ref bm_daq_frontend_init。
 */
typedef struct {
    bm_daq_frontend_config_t config; /**< 轴配置（用户填写） */
    bm_daq_frontend_state_t  state;  /**< 运行状态（由组件维护） */
} bm_daq_frontend_axis_t;

/**
 * @brief 初始化 DAQ 前端轴
 *
 * @param axis                DAQ 轴实例指针，不可为 NULL
 * @param rms_buffer          RMS 滑窗缓冲区，不可为 NULL
 * @param rms_buflen          RMS 缓冲区长度（样本数），须 > 0
 * @param pre_trigger_buffer  预触发环形缓冲区；config.pre_trigger_samples == 0 时可为 NULL
 * @param pre_trigger_buflen  预触发缓冲区长度；config.pre_trigger_samples == 0 时可为 0
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法
 */
int bm_daq_frontend_init(bm_daq_frontend_axis_t *axis,
                         float *rms_buffer,
                         uint32_t rms_buflen,
                         float *pre_trigger_buffer,
                         uint32_t pre_trigger_buflen);

/**
 * @brief 复位 DAQ 前端轴运行状态
 *
 * 清零 peak、triggered、captured 等字段并重置 RMS 滑窗；
 * 缓冲区指针与容量保持不变。NULL 时静默返回。
 *
 * @param axis DAQ 轴实例指针，NULL 时静默返回
 */
void bm_daq_frontend_reset(bm_daq_frontend_axis_t *axis);

/**
 * @brief 武装（arm）DAQ 前端，准备接受采样
 *
 * 置位 armed，清零 triggered/captured/peak 及预触发游标。
 * 调用 @ref bm_daq_frontend_feed 前必须先调用本函数。
 *
 * @param axis DAQ 轴实例指针，NULL 时静默返回
 */
void bm_daq_frontend_arm(bm_daq_frontend_axis_t *axis);

/**
 * @brief 推入一个采样值，驱动触发与采集逻辑
 *
 * @param axis   DAQ 轴实例指针，不可为 NULL
 * @param sample 当前采样值
 * @return BM_OK 继续采集；BM_DAQ_CAPTURE_DONE 后触发段采集完成；
 *         BM_ERR_INVALID 参数为 NULL 或轴未处于 armed 状态
 */
int bm_daq_frontend_feed(bm_daq_frontend_axis_t *axis, float sample);

/**
 * @brief 按时间顺序复制预触发环形缓冲内容到目标数组
 *
 * dst[0] 为最旧样本，dst[n-1] 为最新样本。
 * 实际复制数取 min(pre_trigger_count, dst_len)。
 *
 * @param axis    DAQ 轴实例指针（const），NULL 时返回 0
 * @param dst     目标浮点数组，NULL 时返回 0
 * @param dst_len 目标数组可容纳的最大样本数，0 时返回 0
 * @return 实际复制的样本数；预触发缓冲为空或参数非法时返回 0
 */
uint32_t bm_daq_frontend_copy_pre_trigger(const bm_daq_frontend_axis_t *axis,
                                          float *dst,
                                          uint32_t dst_len);

#ifdef __cplusplus
}
#endif

#endif /* BM_DAQ_FRONTEND_H */
