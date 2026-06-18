# bms_supervision 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / Pack 限值 + fault_derating 嵌入

Not validated: 电芯级诊断、均衡、实机 BMS 拓扑

## 范围

- 电压/电流/温度越限触发 `bm_fault_derating_latch`
- 内嵌 `bm_fault_derating_axis_t` 输出降额因子

## 已知限制

- 单 Pack 聚合限值
- 无故障恢复外部确认
