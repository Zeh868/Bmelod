# STM32G4 Port 实机说明（sdk_stm32g4）

> 状态：已在正点原子 **ATK-DMG474**（STM32G474VET6）完成冒烟验收（2026-07-30）。

## 1. Port 组成

| 层 | 路径 | 说明 |
|----|------|------|
| Pack | `portable/packs/sdk_stm32g4/` | `BM_BACKEND=sdk_stm32g4` = arch + vendor |
| Arch | `portable/arch/armv7em/` | Cortex-M4F 临界区 / FPU 守卫 |
| Vendor | `portable/vendor/stm32g4/` | LL 外设后端（刻意不链完整 HAL API） |
| 工具链 | `cmake/toolchain-arm-none-eabi-g4.cmake` | `-mcpu=cortex-m4 -mfpu=fpv4-sp-d16` |
| Cube 注入 | `cmake/bm_sdk_stm32g4.cmake` | CMSIS/LL 头与所用 LL `.c` |

外设细节与已知限制见 [`portable/vendor/stm32g4/README.md`](../../portable/vendor/stm32g4/README.md)。

## 2. Cube / LL SDK 放置

厂商包**不进框架仓库**，推荐：

```text
D:\Code\Bmelod-sdks\stm32\STM32CubeG4\
  Drivers\CMSIS\
  Drivers\STM32G4xx_HAL_Driver\   ← 含 LL；常为 Cube submodule
```

构建时：

```text
-DBM_STM32_CUBE_PATH=D:/Code/Bmelod-sdks/stm32/STM32CubeG4
```

说明见 `D:\Code\Bmelod-sdks\stm32\README.md`。

Bmelod G4 后端只用 **LL**（`USE_FULL_LL_DRIVER`），不依赖完整 HAL 驱动调用；
包内仍保留 `STM32G4xx_HAL_Driver` 目录名（ST 官方布局）。

## 3. 已验证冒烟（ATK-DMG474）

| 项 | 结果 |
|----|------|
| 板卡 / MCU | 正点原子 ATK-DMG474 / STM32G474VET6 |
| 工程 | [`board/atk_dmg474_smoke/`](../../board/atk_dmg474_smoke/) |
| 构建 | `powershell -File tools/board/build_atk_dmg474_smoke.ps1` |
| 调试 | J-Link SWD |
| 日志 | SEGGER RTT Viewer Channel 0，周期心跳 |
| 时钟 | 首版 Cube 默认 HSI ~16MHz（未上 HSE/PLL） |
| LED | 默认 PF9 低有效（请对照原理图；错脚不影响 RTT） |

**已通过**：上电烧写 + RTT 心跳。  
**未覆盖**：RS485 / FDCAN / NVS 落盘 / 电机 / HSE 170MHz / WCET。

逐步说明见 [`board/atk_dmg474_smoke/README.md`](../../board/atk_dmg474_smoke/README.md)。

## 4. 新建 G4 应用的最小步骤

1. 准备 Cube 路径（上节）。
2. CMake：工具链文件 + `BM_BACKEND=sdk_stm32g4` + `BM_STM32_CUBE_PATH`。
3. 应用源：Cube `startup_stm32g474xx.s` + `system_stm32g4xx.c` + 板级 linker（Flash/RAM 按封装）。
4. 链接：`bm_framework` + `bm_hal_stm32g4`。
5. 日志：真机推荐 RTT（`BM_ENABLE_SEGGER_RTT=ON`，`BM_CONFIG_CONSOLE_LOG_BACKEND=3`）；
   或 UART（默认 LPUART1，引脚见 `bm_hal_instances_stm32g4.h`，须按板覆盖）。
6. 引脚：在包含 instances 前 `#define` 覆盖，或板级头集中管理。

可直接复制 `board/atk_dmg474_smoke/` 改主逻辑。

## 5. 与电机 / 全功能验收的关系

冒烟只证明：**工具链 + startup + vendor 链接 + RTT 通路**。

电流环 / PWM / ADC / 编码器等仍按 [`board/sdk_stm32g4_CHECKLIST.md`](../../board/sdk_stm32g4_CHECKLIST.md)
与包络头 `bm_board_envelope_stm32g4.h` 推进；旧电机一键脚本已下线，新 Demo
应以 `atk_dmg474_smoke` 的 CMake 结构为模板扩展。
