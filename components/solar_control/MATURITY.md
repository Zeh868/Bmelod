# solar_control 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / P&O 与增量电导 MPPT + 功率限幅 / CMD_ENABLED·FAULT 状态机

Not validated: 实机 PV 曲线、阴影遮挡、温度系数

## 范围

- IV 采样 read 回调
- MPPT 参考电压编排
- 超功率限幅降额（缩放 v_ref）
- 使能/故障状态机（对齐 `power_control`）：默认未使能；`bm_solar_control_apply_command` / 可选 `read_command`；`BM_SOLAR_CTRL_CMD_ENABLED` / `_FAULT`；读失败锁存故障并发布 `TEL_FAULT`（兼保留 `TEL_STALE`）

## 已知限制

- 无限功率跟踪与 IV 扫描
- 限功率为简单比例缩放，非 MPPT 重寻优
- **行为变更（2026-08-01）：** 旧调用方若不先 `apply_command` 置 `CMD_ENABLED`，`step` 不再跑环
