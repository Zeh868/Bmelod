# bms_supervision 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / Pack 限值 + common 降额服务绑定

Not validated: 电芯级诊断、均衡、实机 BMS 拓扑

## 范围

- 电压/电流/温度越限通过 `bm_derating_service_t` 触发降额
- 使用 common 零组件依赖契约读取降额因子，不绑定其他组件轴类型

## 已知限制

- 单 Pack 聚合限值
- 无故障恢复外部确认
