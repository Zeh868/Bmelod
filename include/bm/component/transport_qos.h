/**
 * @file transport_qos.h
 * @brief 传输 QoS：延迟与抖动监控骨架
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-17       0.2            zeh            token bucket 有界整形
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

int  bm_transport_qos_validate_config(const bm_transport_qos_config_t *config);
int  bm_transport_qos_init(bm_transport_qos_axis_t *axis);
void bm_transport_qos_reset(bm_transport_qos_axis_t *axis);
void bm_transport_qos_on_tx(bm_transport_qos_axis_t *axis);
void bm_transport_qos_on_rx(bm_transport_qos_axis_t *axis);
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
