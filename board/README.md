# STM32G4 共享板级包络（M5 起步）

本目录提供跨 Demo 复用的 `bm_board_envelope_stm32g4.h`，并增加 WCET 占位宏供实机 profiling 填写。

实机 WCET 填写请使用 [WCET_REPORT_TEMPLATE.md](WCET_REPORT_TEMPLATE.md) 表格模板。

PIL/HIL 证据归档请使用 [PIL_EVIDENCE_TEMPLATE.md](PIL_EVIDENCE_TEMPLATE.md)（含 QEMU 周期回归占位示例）。

QEMU 基线 JSON 示例（与 [`tools/measure_wcet.py`](../tools/measure_wcet.py) 输出格式一致）：[`reports/qemu_baseline.example.json`](reports/qemu_baseline.example.json)

QEMU 仿真 WCET **填写示例**（诚实标注非实机 µs）：[`reports/WCET_QEMU_FILLED.example.md`](reports/WCET_QEMU_FILLED.example.md)

STM32G4 PIL **待实机填写示例**（诚实标注，非验收证据）：[`reports/PIL_STM32G4_FILLED.example.md`](reports/PIL_STM32G4_FILLED.example.md)

实机验收清单：[sdk_stm32g4_CHECKLIST.md](sdk_stm32g4_CHECKLIST.md)

`sdk_stm32g4` 后端（`portable/packs/sdk_stm32g4` = arch/armv7em + vendor/stm32g4）
的 compile+link 验证以本地 smoke 工程 `build_tests_stm32g4/smoke/`（gitignore
覆盖的验证稿，不进 git）为参照：arm-none-eabi-gcc 13.3 +
`-DBM_BACKEND=sdk_stm32g4` + `-DBM_STM32_CUBE_PATH=<STM32CubeG4 根>`，
最小 main（1kHz tick + uptime + console + IWDG）全量链接出 `.elf`。
（原一键构建脚本 `tools/board/build_stm32g4_motor_foc.ps1` 已随 motor_foc
Demo 下线，重建电机 Demo 后按 smoke 工程结构补齐。）

QEMU 仿真周期基线采集（封装 `measure_wcet.py`，输出 JSON 至 `board/reports/`）：

```powershell
.\tools\board\run_qemu_wcet_baseline.ps1 -Label stream_array_mvdr -Demo stream_array_mvdr
```

参数说明：

| 参数 | 说明 |
|------|------|
| `-Label` | 报告标签（必填），写入 JSON `label` 字段 |
| `-Demo` | Demo 目录名，默认 `stream_array_mvdr` |
| `-BuildDir` | CMake 构建目录，默认 `build/qemu_wcet` |
| `-OutputDir` | JSON 输出目录，默认 `board/reports` |

输出文件形如 `board/reports/qemu_<Label>.json`，字段与 [`reports/qemu_baseline.example.json`](reports/qemu_baseline.example.json) 一致；`hardware_wcet` 恒为 `false`（仿真周期，非实机 µs）。

## 职责划分

| 侧 | 内容 |
|----|------|
| **框架** | 共享包络模板、Demo 组件、native_sim 验收 |
| **用户** | 填写电气/时序/WCET、`sdk_stm32g4` HAL 实例、ADC/PWM/编码器标定 |

## sdk_stm32g4 对接步骤

1. 复制或 `#include` 本目录 `bm_board_envelope_stm32g4.h`，按硬件填写 R、极对数、CPR、PWM/采样频率。
2. 在 `portable/vendor/stm32g4/bm_hal_instances_stm32g4.h` 绑定 TIM/ADC/ENC 实例（全部可覆盖宏，默认 NUCLEO-G474RE）。
3. 应用工程构建（以 smoke 工程 `build_tests_stm32g4/smoke/` 的结构为参照）：

```bash
cmake -S <应用工程> -B <构建目录> -G Ninja \
  -DBM_BACKEND=sdk_stm32g4 \
  -DBM_STM32_CUBE_PATH=/path/to/STM32CubeG4 \
  -DBM_STM32_DEVICE=STM32G474xx \
  -DBM_ENABLE_ALGORITHM=ON \
  -DBM_ENABLE_COMPONENT_MOTOR=ON \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi-g4.cmake
```

启动文件/链接脚本经 `BM_STM32G4_STARTUP` / `BM_STM32G4_LD` 从 Cube 包引用，
`system_stm32g4xx.c` 由应用工程从 Cube 引入（vendor 不接管时钟树），
详见 `cmake/bm_sdk_stm32g4.cmake` 头注释与
[`portable/vendor/stm32g4/README.md`](../portable/vendor/stm32g4/README.md)。
（电机 Demo 重建后此处再补一键构建脚本。）

4. 按 [sdk_stm32g4_CHECKLIST.md](sdk_stm32g4_CHECKLIST.md) 完成实机验收。
5. 电流采样极性 → Clarke/Park 链标定；记录 WCET 余量与跟踪指标。

## 成熟度

当前为 **E1** 模板（占位 WCET=0）。实机 V2 需 PIL/HIL 与 profiling 证据，见各组件 `MATURITY.md`。
