# process_sequence 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / TON/TOF 计数、N 步驻留顺序机、联锁资源与 exec 生命周期适配

Not validated: IEC 61131-3 完整语义、在线修改、冗余联锁

## 范围

- `bm_process_ton_step` / `bm_process_tof_step`
- `bm_process_sequence_start` + 联锁回调 + 逐步驻留
- 独立 `bm_process_sequence_exec_context_t` 的 init/start/run/safe_stop 生命周期
- 既有 `bm_process_sequence_axis_t` 布局、指定初始化器与 step 调用兼容性

## 已知限制

- 最多 8 步；每步仅驻留时间与单一联锁回调
- exec 适配上下文由用户静态分配，不管理 axis 与回调用户上下文的生命周期
- 非认证级 PLC runtime
