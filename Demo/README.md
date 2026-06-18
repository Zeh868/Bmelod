# Demo/

Bmelod 示例源码（类比 FreeRTOS/Demo/）。**仅含应用代码与 `CMakeLists.txt`**，不含构建产物。

每个示例子目录均含 **`README.md`**（中文）：概述、学习重点、构建命令与验收标准。

## 示例索引

| 示例 | 层级 | 说明 |
|------|------|------|
| [ultra_blink](ultra_blink/README.md) | Ultra | 极简事件队列 + LED |
| [core_sensor](core_sensor/README.md) | Nano | 模块 + 事件 + 内存池 |
| [interrupt_demo](interrupt_demo/README.md) | Nano | ISR → 事件入队 |
| [full_system](full_system/README.md) | Lite | 全系统 + 看门狗 + 故障 |
| [hrt_servo_stub](hrt_servo_stub/README.md) | Control | HRT 伺服桩 |
| [hrt_bms_coulomb](hrt_bms_coulomb/README.md) | Control | HRT 库仑计 |
| [closed_loop_servo](closed_loop_servo/README.md) | Control | 速度环闭环 |
| [foc_current_loop](foc_current_loop/README.md) | Control | FOC 电流环 |
| [motor_foc_sensored](motor_foc_sensored/README.md) | Control | 完整有感 FOC |
| [power_buck_loop](power_buck_loop/README.md) | Control | Buck 双环电源 |
| [bms_estimation_loop](bms_estimation_loop/README.md) | Control | BMS SOC 估算 |
| [multi_axis_sync](multi_axis_sync/README.md) | Control | 多轴同步 |
| [multi_channel_bms](multi_channel_bms/README.md) | Control | 多通道 BMS |
| [stream_block_rms](stream_block_rms/README.md) | DSP | 块流 RMS |
| [daq_frontend](daq_frontend/README.md) | DSP | DAQ 触发采集 |
| [stream_fft](stream_fft/README.md) | DSP | 块流 FFT |
| [stream_bms_pipeline](stream_bms_pipeline/README.md) | DSP/BMS | 块流 BMS 管线 |
| [stream_audio_agc](stream_audio_agc/README.md) | DSP | 音频 AGC 管线 |
| [stream_vibration_diag](stream_vibration_diag/README.md) | DSP | 振动 Goertzel 诊断 |
| [stream_vision_frame](stream_vision_frame/README.md) | DSP | 视觉帧差质心 |
| [ultrasonic_frontend](ultrasonic_frontend/README.md) | DSP | 超声 ToF 检波 |

## 运行

```powershell
.\tools\demo\run_native.ps1 core_sensor
.\tools\demo\run_qemu.ps1 interrupt_demo
.\tools\demo\run_all.ps1
```

构建产物统一在仓库根目录 **`build/demo/<variant>/`**：每个 variant 仅一份 CMake 树（`bmelod/` 中间文件共享），各示例 ELF/EXE 在 **`build/demo/<variant>/<example>/`**。详见 [tools/demo/README.md](../tools/demo/README.md)。

## 文档

- [01-Demo示例与运行路径](../docs/01-应用开发/01-Demo示例与运行路径.md)
- [PORTING.md](PORTING.md) — 移植到真实硬件
