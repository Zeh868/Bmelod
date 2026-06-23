/**
 * @file transport_qos.h
 * @brief 传输 QoS：延迟与抖动监控骨架
 *
 * 基于 on_tx/on_rx 钩子测量单程延迟，EMA 滤波后触发报警；
 * 集成 token bucket 对出队字节进行整形。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.3
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-17       0.2            zeh            token bucket 有界整形
 * 2026-06-23       0.3            zeh            补 SPDX 与函数级 Doxygen
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_TRANSPORT_QOS_H
#define BM_TRANSPORT_QOS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_TRANSPORT_QOS_TEL_VALID   (1u << 0u)
#define BM_TRANSPORT_QOS_TEL_ALARM (1u << 1u)

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    latency_ms;
    float    jitter_ms;
    float    latency_ema_ms;
} bm_transport_qos_telemetry_t;

typedef uint32_t (*bm_transport_qos_now_ms_fn)(void *user);

typedef void (*bm_transport_qos_publish_fn)(
    void *user,
    const bm_transport_qos_telemetry_t *telemetry);

typedef struct {
    bm_transport_qos_now_ms_fn    now_ms;
    void                         *now_ms_user;
    bm_transport_qos_publish_fn  publish_telemetry;
    void                         *publish_telemetry_user;
} bm_transport_qos_resources_t;

typedef struct {
    float    latency_alarm_ms;
    float    jitter_alarm_ms;
    float    ema_alpha;
    float    token_rate_bytes_per_ms;
    uint32_t token_burst_bytes;
} bm_transport_qos_config_t;

typedef struct {
    uint32_t prev_tx_ms;
    uint32_t prev_rx_ms;
    float    latency_ms;
    float    jitter_ms;
    float    latency_ema_ms;
    float    tokens;
    int      have_prev;
    uint32_t step_count;
    bm_transport_qos_telemetry_t telemetry;
} bm_transport_qos_state_t;

typedef struct {
    bm_transport_qos_config_t    config;
    bm_transport_qos_resources_t resources;
    bm_transport_qos_state_t     state;
} bm_transport_qos_axis_t;

/**
 * @brief 校验 QoS 配置参数
 * @param config 配置结构指针（不可为 NULL）
 * @return BM_OK 合法；BM_ERR_INVALID 无效
 */
int  bm_transport_qos_validate_config(const bm_transport_qos_config_t *config);

/**
 * @brief 初始化 QoS 轴（校验配置后复位，并预填令牌桶）
 * @param axis QoS 轴实例（不可为 NULL；config 须预先填写）
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效
 */
int  bm_transport_qos_init(bm_transport_qos_axis_t *axis);

/**
 * @brief 复位 QoS 状态（清零延迟/抖动/令牌与遥测）
 * @param axis QoS 轴实例（NULL 时直接返回）
 */
void bm_transport_qos_reset(bm_transport_qos_axis_t *axis);

/**
 * @brief 记录当前时刻为发送时间戳（在数据包发出时调用）
 * @param axis QoS 轴实例（NULL 或无 now_ms 回调时直接返回）
 */
void bm_transport_qos_on_tx(bm_transport_qos_axis_t *axis);

/**
 * @brief 在收到数据包时计算延迟与抖动并更新 EMA
 * @param axis QoS 轴实例（NULL 或无 now_ms 回调时直接返回）
 */
void bm_transport_qos_on_rx(bm_transport_qos_axis_t *axis);

/**
 * @brief 周期性步进：超时检测、报警判断、更新遥测并发布
 * @param axis QoS 轴实例（NULL 时直接返回）
 */
void bm_transport_qos_step(bm_transport_qos_axis_t *axis);

/**
 * @brief token bucket 入队整形
 *
 * @param axis QoS 轴实例
 * @param bytes 待发送字节数
 * @return 0 接受；-1 丢弃（桶空或参数无效）
 */
int bm_transport_qos_enqueue(bm_transport_qos_axis_t *axis, uint32_t bytes);

#ifdef __cplusplus
}
#endif

#endif /* BM_TRANSPORT_QOS_H */
