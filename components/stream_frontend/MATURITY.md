# stream_frontend 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / bm_stream 提交消费与漂移统计

Not validated: 多核 relay、实机 DMA 块流

## 范围

- 包装 `bm_stream`/`bm_block`：`on_block_submit` / `on_block_consume`
- `bm_algo_clock_drift_feed` / `compensate` 块间隔统计
- 对接 `bm_stream_stats` overrun/underrun/late

## 已知限制

- 单核 owner_cpu 假设
- 漂移补偿仅更新遥测，不自动重采样
