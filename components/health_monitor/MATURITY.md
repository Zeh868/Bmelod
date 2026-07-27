# health_monitor 成熟度

Maturity: E1 - 前期应用探索

Validated: native_sim / 多源聚合、锁存与清除、遥测变更发布

Not validated: 实机多组件接入、与 shell/上位机查询通路集成、WCET

## 范围

- `bm_health_monitor_report`：按 source_id 上报统一故障码（`bm_fault.h`），
  `BM_FAULT_NONE` 清除活动故障、保留锁存
- 系统级健康快照：最严重严重度、活动/锁存源计数、最重故障源
- 快照变化时经 `BM_COMPONENT_PUBLISH_TELEMETRY` 发布遥测

## 已知限制

- 故障源按 source_id 线性查表，源数量须由应用控制在小规模
- 单故障码/源：同一源并发多种故障时由应用决定上报优先级
- 无持久化：锁存记录仅存 RAM，复位/掉电丢失
