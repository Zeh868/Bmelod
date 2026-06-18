# process_sequence 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / TON/TOF 计数与 N 步超时顺序机

Not validated: IEC 61131-3 完整语义、在线修改、冗余联锁

## 范围

- `bm_process_ton_step` / `bm_process_tof_step`
- `bm_process_sequence_start` + 联锁回调 + 逐步超时

## 已知限制

- 最多 8 步；每步仅超时与单一联锁回调
- 非认证级 PLC runtime
