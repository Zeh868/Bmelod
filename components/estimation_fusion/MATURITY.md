# estimation_fusion 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / 互补 / Mahony / EKF-CV 模式切换骨架

Not validated: 磁力计融合、模式切换无扰、实机 IMU 标定

## 范围

- `bm_estimation_fusion_mode_t` 选择底层 K0 算法
- IMU 六轴经 resources 回调注入

## 已知限制

- EKF 模式为占位（位置轴），非完整姿态 EKF
- 无自动模式降级
