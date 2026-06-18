# process_control 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / Smith + 双 PID 串级骨架

Not validated: 被控对象建模、抗饱和、实机 HVAC/灌溉 Demo

## 范围

- 外环 PID → Smith 预估 → 内环 PID → 执行器回调
- 应用提供 Smith 延迟线缓冲

## 已知限制

- 内外环测量共用同一反馈
- 无模式切换与故障处理
