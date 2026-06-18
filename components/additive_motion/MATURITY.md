# additive_motion 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / ZV 两脉冲输入整形骨架

Not validated: 实机 Z 轴共振标定、压力 advance 模型

## 范围

- Z 轴 ZV（Zero Vibration）两脉冲输入整形
- 自然频率与阻尼比配置
- 环形延迟缓冲

## 已知限制

- 仅 Z 轴标量整形，无 XY 耦合
- 未实现 pressure advance 线性挤出模型（后续批次）
- 阻尼比需 >0 且 <1，否则系数退化为近似
