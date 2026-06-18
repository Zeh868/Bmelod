# transport_qos 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / TX-RX 延迟与抖动 EMA

Not validated: 时钟同步、多流聚合、协议栈集成

## 范围

- `bm_transport_qos_on_tx/on_rx` 事件 + 周期 `step` 告警判定
- 应用注入 `now_ms` 单调时钟

## 已知限制

- 单链路单向 RTT 近似
- 无丢包与乱序统计
