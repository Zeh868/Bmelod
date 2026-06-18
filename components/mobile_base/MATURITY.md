# mobile_base_control 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / 差速 v,ω→左右轮速

Not validated: 实机打滑补偿、里程计闭环

## 范围

- 差速运动学：left = v - ω·L/2，right = v + ω·L/2
- 轮速饱和
- 可选坡道 sin(θ) 前馈骨架

## 已知限制

- 无麦克纳姆/全向模型
- 坡道前馈为简化重力项，未标定质量
- 输出为线速度 m/s，rpm 换算由调用者完成
