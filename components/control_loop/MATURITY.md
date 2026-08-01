# control_loop 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / 双环 PI 串级骨架 / CMD_ENABLED·FAULT 状态机

Not validated: 实机 HAL、多轴调度、WCET、前馈与扰动观测

## 范围

- 外环 PI 跟踪设定、输出作内环设定
- 内环 PI 跟踪外环输出
- 饱和与积分抗饱和复用 `bm_algo_pi`
- 四段式 API：`config` / `resources` / `state` / `axis`，提供 `bm_control_loop_init` 与 `bm_control_loop_exec_ops`
- `init` / `validate` 复用 `bm_component_common.h` 公共宏（`BM_COMPONENT_INIT`、`BM_COMPONENT_RETURN_IF_NULL`、`BM_COMPONENT_VALIDATE_POSITIVE_FLOAT`）
- 使能/故障状态机（对齐 `power_control`）：默认未使能；`bm_control_loop_apply_command` / 可选 `read_command`；`BM_CONTROL_LOOP_CMD_ENABLED` / `_FAULT`；反馈失败锁存故障

## 已知限制

- 仅 PI（无 D 项、无前馈）
- `read_plant` 回调须在同一步提供外环/内环测量与设定
- 无遥测发布、无 Smith/扰动补偿（见 `process_control`）
- 单轴实例，无多轴编排
- **行为变更（2026-08-01）：** 旧调用方若不先 `apply_command` 置 `CMD_ENABLED`，`step` 不再跑环
