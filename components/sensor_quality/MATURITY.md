# sensor_quality 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / 范围 + 变化率 + 冻结值检测

Not validated: 多通道聚合、自适应阈值、实机噪声模型

## 范围

- `bm_sensor_quality_step`：采样回调 + `bm_algo_range_monitor`
- 冻结值：连续 N 次变化低于 epsilon 置 `BM_ALGO_FAULT_FROZEN`

## 已知限制

- 单通道浮点样本
- 冻结检测为简单计数器，无滑动窗口
