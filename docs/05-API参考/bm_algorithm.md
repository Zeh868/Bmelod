# bm_algorithm — 确定性纯算法库

头文件：`include/bm_algorithm.h`（聚合）、`include/bm/algorithm/bm_algo_*.h`  
实现：`Source/algorithm/`  
成熟度：**E1**（前期应用探索，float32，PC 单元测试）

## 概述

`bm_algorithm` 提供无框架状态、无 HAL 依赖的纯数学核（K0）。算法通过显式
`state` / `config` 分离支持多实例；`step` 接受显式 `dt_s` 或预计算系数。

**不包含**：`bm_exec` 领域组件、协议栈、定点 Q 格式后缀、工业量产证据。

示例与 Demo 说明见 [01-Demo示例与运行路径](../01-应用开发/01-Demo示例与运行路径.md)。

## 构建与包含

```cmake
option(BM_ENABLE_ALGORITHM "Enable bm_algorithm" OFF)
# 单元测试构建时自动 ON
target_link_libraries(my_app PRIVATE bm_algorithm)
```

```c
#include "bm_algorithm.h"   /* 或按需包含 bm/algorithm/bm_algo_*.h */
```

`bm_algorithm` 只链接 `bm_config`（及非 MSVC 下的 `libm`），**不**依赖 `bm_core`。

## 算法族索引

| 头文件 | API 前缀 | 用途 |
|--------|----------|------|
| `bm_algo_common.h` | `bm_algo_clamp_f`、`bm_algo_deadband_f`… | 饱和、死区、滞环、速率限制、角度 |
| `bm_algo_control.h` | `bm_algo_pi_*`、`bm_algo_pid_*`… | PI/PID、PR、积分器、超前滞后 |
| `bm_algo_filter.h` | `bm_algo_lpf1_*`、`bm_algo_biquad_*`… | LPF/HPF、FIR、陷波 |
| `bm_algo_profile.h` | `bm_algo_ramp_*`、`bm_algo_trapezoid_*`… | 斜坡、梯形、S 曲线 |
| `bm_algo_motion.h` | `bm_algo_encoder_*`、`bm_algo_dda_*` | 编码器、DDA |
| `bm_algo_motor.h` | `bm_algo_clarke`、`bm_algo_svpwm`… | Clarke/Park、SVPWM |
| `bm_algo_power.h` | `bm_algo_sogi_pll_*`、`bm_algo_mppt_*`… | PLL、MPPT、RMS |
| `bm_algo_battery.h` | `bm_algo_coulomb_*`、`bm_algo_ocv_*`… | 库仑、OCV-SOC、SOH |
| `bm_algo_fft.h` | `bm_algo_rfft_f32_*`… | CFFT/RFFT、窗函数 |
| `bm_algo_spectral.h` | `bm_algo_goertzel_*`… | Goertzel、PSD、包络 |
| `bm_algo_resample.h` | `bm_algo_decimator_*`… | 抽取、线性重采样 |
| `bm_algo_fusion.h` | `bm_algo_mahony_*`、`bm_algo_madgwick_*`… | 姿态融合 |
| `bm_algo_audio.h` | `bm_algo_agc_*`、`bm_algo_vad_*`… | 音频处理 |
| `bm_algo_image.h` | `bm_algo_image_threshold_u8`… | 低分辨率图像算子 |
| `bm_algo_features.h` | `bm_algo_quantize_*`… | TinyML 前后处理 |
| `bm_algo_calibration.h` | `bm_algo_lut1d_interp`… | 标定与插值 |
| `bm_algo_statistics.h` | `bm_algo_stats_*`… | 统计量 |
| `bm_algo_comm.h` | `bm_algo_crc16_ccitt`、`bm_algo_dtmf_*`… | CRC、DTMF |
| `bm_algo_signal_quality.h` | `bm_algo_debounce_analog_*`… | 去抖、范围监控 |
| `bm_algo_estimator.h` | `bm_algo_kalman1d_*`、`bm_algo_ekf_cv_*`… | 卡尔曼 / EKF |

## 典型用法（PI）

```c
bm_algo_pi_config_t cfg = {
    .kp = 2.0f,
    .ki = 50.0f,
    .out_min = -1.0f,
    .out_max = 1.0f,
    .integrator_min = -10.0f,
    .integrator_max = 10.0f
};
bm_algo_pi_state_t st;

bm_algo_pi_reset(&st, 0.0f);
float u = bm_algo_pi_step(&st, &cfg, error_rad, dt_s);
```

## 与框架集成

| 层 | 职责 |
|----|------|
| `bm_algorithm` | 纯 `step()` 数学计算 |
| `bm_exec` | 采样标定、槽位编排、HAL 访问 |
| `bm_module` | 模式、故障、命令、诊断 |

不要把 `bm_algo_pi_step()` 注册为 `bm_module`；在 `bm_exec` 的 HRT 槽内调用算法 `step`。

## 测试

```bash
cmake -B build -DBM_BUILD_TESTS=ON
cmake --build build
ctest -C Release -R "^test_algo_" --output-on-failure
```

## 相关文档

- [01-Demo示例与运行路径](../01-应用开发/01-Demo示例与运行路径.md)
- [01-CMake选项与bm_config](../02-构建与工具链/01-CMake选项与bm_config.md)
