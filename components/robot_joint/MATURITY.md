# robot_joint_control 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / 单轴 PI 力矩 + 摩擦前馈骨架

Not validated: 实机关节动力学、双编码器、碰撞检测

## 范围

- resources 读位置/速度、写力矩
- PI 位置环 + 摩擦补偿 `bm_algo_friction_comp`
- 位置/速度/力矩限幅

## 已知限制

- 单轴独立控制，无多轴协调
- 无重力/科氏力补偿
- PI 参数需现场整定
