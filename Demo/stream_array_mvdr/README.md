# stream_array_mvdr

M7 K2 帧流 pipeline 深化示例：多通道正弦块流（固定时延差）→ `bm_audio_array_frontend` MVDR → `bm_pipeline` LPF 后处理 → 块能量/RMS 遥测。

## 构建

```bash
cmake -B build/demo/native -S Demo -DBMELOD_DEMO_NATIVE=ON
cmake --build build/demo/native --target stream_array_mvdr
```

## 验收

native_sim 运行后输出 `PASS STREAM_ARRAY_MVDR`；块处理数、RMS 与能量超过阈值。
