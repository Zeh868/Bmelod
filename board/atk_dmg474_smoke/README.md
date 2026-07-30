# ATK-DMG474 真机冒烟（STM32G474VET6 + J-Link RTT）

> **实机状态（2026-07-30）：已通过** — 上电烧写后 RTT Viewer 可见周期心跳。

最小工程：烧写固件后，用 **J-Link RTT Viewer** 看每秒心跳日志；可选翻转 LED。

Port 总览：[`docs/03-移植与IDE集成/10-STM32G4-Port实机.md`](../../docs/03-移植与IDE集成/10-STM32G4-Port实机.md)。

## 前置

| 项 | 要求 |
|----|------|
| 工具链 | `arm-none-eabi-gcc`（PATH 可找到） |
| 构建 | CMake ≥ 3.20 + Ninja |
| Cube | 默认 `D:\Code\Bmelod-sdks\stm32\STM32CubeG4`；可改 `-CubePath` / `BM_STM32_CUBE_PATH` |

| 调试器 | **J-Link**（官方 RTT Viewer） |

## 一键构建

在仓库根目录：

```powershell
powershell -File tools/board/build_atk_dmg474_smoke.ps1
```

产物：

- `build_atk_dmg474_smoke/atk_dmg474_smoke.elf`
- `build_atk_dmg474_smoke/atk_dmg474_smoke.bin`

手动等价：

```powershell
cmake -S board/atk_dmg474_smoke -B build_atk_dmg474_smoke -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi-g4.cmake `
  -DBM_STM32_CUBE_PATH=D:/Code/Bmelod-sdks/stm32/STM32CubeG4
cmake --build build_atk_dmg474_smoke
```

## 烧写（J-Link）

示例（按本机 J-Link 路径调整）：

```text
JLinkExe
> device STM32G474VE
> if SWD
> speed 4000
> connect
> loadfile build_atk_dmg474_smoke/atk_dmg474_smoke.elf
> r
> g
```

或用 Ozone / VS Code Cortex-Debug 加载同一 `.elf`。

## RTT Viewer

1. 打开 **J-Link RTT Viewer**
2. 连接：USB，Device = `STM32G474VE`，Target Interface = SWD
3. Channel 0，ASCII
4. 复位后应看到类似：

```text
[I][atk] ATK-DMG474 smoke start (G474VE, RTT)
[I][atk] SystemCoreClock=16000000 Hz
[I][atk] heartbeat beat=0 uptime_ms=...
[I][atk] heartbeat beat=1 uptime_ms=...
```

（首版时钟为 Cube 默认 HSI ~16MHz；后续可再上 HSE/PLL。）

## LED

默认尝试 **PF9**（低电平亮，常见灌电流）。请对照正点原子原理图核对：

- 改脚：编辑 `board_pins.h` 中 `ATK_DMG474_LED0_PIN`
- 关掉 LED：编译加 `-DATK_DMG474_LED_ENABLE=0`，或改宏为 `0`

LED 错脚**不影响** RTT 验收。

## 本轮范围

做：能烧写、RTT 心跳。  
不做：RS485 / CAN / NVS / 电机 / Shell。
