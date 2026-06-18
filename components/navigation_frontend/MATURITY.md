# navigation_frontend 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / GNSS+轮速时间对齐 + EKF 门控融合骨架

Not validated: 实机 GNSS/IMU 标定、多路径、航向融合

## 范围

- 三源 read 回调（GNSS 速度、IMU 陀螺、轮速 rpm）
- 时间戳 mailbox 对齐（容差内融合）
- `bm_algo_ekf_gate` 门控更新

## 已知限制

- 仅输出标量融合速度，无 2D/3D 导航解
- IMU 陀螺未参与速度积分（占位读取）
- 对齐失败时回退轮速或 EKF 预测
