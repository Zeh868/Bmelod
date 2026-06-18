# control_loop 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / 双环 PI 串级骨架

Not validated: 实机 HAL、多轴调度、WCET、前馈与扰动观测

## 范围

- 外环 PI 跟踪设定、输出作内环设定
- 内环 PI 跟踪外环输出
- 饱和与积分抗饱和复用 `bm_algo_pi`

## 已知限制

- 仅 PI（无 D 项、无前馈）
- `read_plant` 回调须在同一步提供外环/内环测量与设定
- 无遥测发布、无 Smith/扰动补偿（见 `process_control`）
- 单轴实例，无多轴编排
