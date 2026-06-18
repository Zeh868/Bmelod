# radar_frontend 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / RFFT 距离像与均值杂波相减

Not validated: 实机 FMCW 标定、多目标 CFAR

## 范围

- 快时间 `bm_algo_rfft_f32` 距离幅度谱
- 简易指数均值杂波抑制（E1）
- chirp 块输入 → 峰值距离与幅度遥测

## 已知限制

- 单 chirp、无多普勒处理
- 距离刻度为简化 bin×scale，未做 chirp 斜率补偿
