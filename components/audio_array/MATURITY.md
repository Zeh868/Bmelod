# audio_array_frontend 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / 固定时延 DAS 与对角加载 MVDR

Not validated: 实机麦克风阵列标定、宽带 GCC-PHAT 鲁棒性、完整自适应 MVDR

## 范围

- 最多 4 通道延迟对齐 + delay-and-sum 或对角加载 MVDR
- 可选 `bm_algo_gcc_phat_delay` 或配置 `fixed_delay_samples`
- `beam_mode`：`BM_AUDIO_BEAM_DAS` / `BM_AUDIO_BEAM_MVDR`
- 每步输出单通道能量

## 已知限制

- MVDR 为块内对角协方差 + 固定 steering（1 kHz 窄带相位近似），非完整宽带自适应 MVDR
- GCC-PHAT 受 FFT 长度与 max_lag 约束
- 无在线协方差矩阵求逆、无多波束扫描
