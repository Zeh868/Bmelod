# grid_control 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / SOGI-PLL + PR 电流环骨架 / CMD_ENABLED·FAULT 状态机

Not validated: dq 变换、并网保护、实机逆变器

## 范围

- 电网电压 SOGI-PLL 锁相
- 单相 PR 电流跟踪（标量占位）
- 使能/故障状态机（对齐 `power_control`）：默认未使能；`bm_grid_control_apply_command` / 可选 `read_command`；`BM_GRID_CTRL_CMD_ENABLED` / `_FAULT`；读失败锁存故障并发布 `TEL_FAULT`（兼保留 `TEL_STALE`）

## 已知限制

- 无电压环与功率因数控制
- 三相系统未展开
- **行为变更（2026-08-01）：** 旧调用方若不先 `apply_command` 置 `CMD_ENABLED`，`step` 不再跑环
