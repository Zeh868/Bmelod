# grid_control 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / SOGI-PLL + PR 电流环骨架 / CMD_ENABLED·FAULT 状态机

Not validated: dq 变换、并网保护、实机逆变器

## 范围

- 电网电压 SOGI-PLL 锁相
- 单相 PR 电流跟踪（标量占位）
- 使能/故障状态机（对齐 `power_control`）：`bm_grid_control_apply_command` / 可选 `read_command`；`BM_GRID_CTRL_CMD_ENABLED` / `_FAULT`；读失败锁存故障并发布 `TEL_FAULT`（兼保留 `TEL_STALE`）
- 使能门控绑定命令通道（2026-08-02）：`read_command==NULL` 时恒使能（legacy 兼容）；绑定后默认未使能、须 `CMD_ENABLED`，未使能时 log-once 告警；fault 锁存无条件生效

## 已知限制

- 无电压环与功率因数控制
- 三相系统未展开
- **行为变更（2026-08-01）：** 引入 CMD_ENABLED/FAULT 门控
- **行为修正（2026-08-02）：** 使能门控仅在绑定 `read_command` 后生效——未接命令通道的旧调用方恢复恒使能，不再被静默零输出；绑定通道的调用方仍须先 `apply_command` 置 `CMD_ENABLED`
