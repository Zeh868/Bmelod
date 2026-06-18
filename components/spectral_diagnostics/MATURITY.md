# spectral_diagnostics 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / Goertzel 单频 + STFT 帧缓冲骨架

Not validated: 实机振动标定、阶次跟踪滤波、WCET

## 范围

- Goertzel 目标频幅值与 `bm_algo_order_from_hz` 阶次换算
- STFT 模式：应用提供 frame/window/magnitude 缓冲

## 已知限制

- STFT 无重叠与窗函数初始化
- 单通道加速度输入
