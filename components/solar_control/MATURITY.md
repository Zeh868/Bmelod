# solar_control 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / P&O 与增量电导 MPPT + 功率限幅

Not validated: 实机 PV 曲线、阴影遮挡、温度系数

## 范围

- IV 采样 read 回调
- MPPT 参考电压编排
- 超功率限幅降额（缩放 v_ref）

## 已知限制

- 无限功率跟踪与 IV 扫描
- 限功率为简单比例缩放，非 MPPT 重寻优
