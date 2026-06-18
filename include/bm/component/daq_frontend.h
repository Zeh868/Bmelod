/**
 * @file daq_frontend.h
 * @brief DAQ 前端：触发、预触发与 RMS/波峰因数捕获
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
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

/** bm_daq_frontend_feed 返回：后触发段采集完成 */
#define BM_DAQ_CAPTURE_DONE  1

typedef struct {
    float    trigger_level;
    uint32_t pre_trigger_samples;
    uint32_t capture_samples;
    uint32_t sample_hz;
} bm_daq_frontend_config_t;

typedef struct {
    bm_algo_rms_config_t   rms_cfg;
    bm_algo_rms_state_t    rms;
    float                 *rms_buffer;
    uint32_t               rms_buflen;
    float                 *pre_trigger_buffer;
    uint32_t               pre_trigger_cap;
    uint32_t               pre_trigger_head;
    uint32_t               pre_trigger_count;
    float                  peak;
    float                  crest_factor;
    uint32_t               captured;
    int                    armed;
    int                    triggered;
} bm_daq_frontend_state_t;

typedef struct {
    bm_daq_frontend_config_t config;
    bm_daq_frontend_state_t  state;
} bm_daq_frontend_axis_t;

int bm_daq_frontend_init(bm_daq_frontend_axis_t *axis,
                         float *rms_buffer,
                         uint32_t rms_buflen,
                         float *pre_trigger_buffer,
                         uint32_t pre_trigger_buflen);
void bm_daq_frontend_reset(bm_daq_frontend_axis_t *axis);
void bm_daq_frontend_arm(bm_daq_frontend_axis_t *axis);
/** @return BM_OK 继续；BM_DAQ_CAPTURE_DONE 采集完成；BM_ERR_INVALID 参数/未武装 */
int bm_daq_frontend_feed(bm_daq_frontend_axis_t *axis, float sample);
/** 按时间顺序复制预触发环形缓冲样本；@return 实际复制的样本数 */
uint32_t bm_daq_frontend_copy_pre_trigger(const bm_daq_frontend_axis_t *axis,
                                          float *dst,
                                          uint32_t dst_len);

#ifdef __cplusplus
}
#endif

#endif /* BM_DAQ_FRONTEND_H */
