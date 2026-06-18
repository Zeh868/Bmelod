# portable/arch — ISA 架构 Port 层

类比 FreeRTOS `portable/GCC/ARM_CMx/`：仅实现临界区、内存屏障、ISR 检测等与 ISA 绑定的原语。

| Arch ID | 目录 | 说明 |
|---------|------|------|
| `stub` | `stub/` | 编译器屏障桩，CI / 无硬件 |
| `host` | `host/` | 宿主仿真通用桩 |
| `armv6m` | `armv6m/` | Cortex-M0/M0+，primask |
| `armv7m` | 别名 → `armv6m` | Cortex-M3 |
| `armv7em` | `armv7em/` | M4/M7，primask + basepri |
| `armv8m_base` | 别名 → `armv6m` | Cortex-M23 |
| `armv8m_main` | `armv8m_main/` | M33/M55，复用 armv7em 源 |
| `riscv32` | `riscv32/` | RV32IMAC，mstatus.MIE + fence |
| `riscv64` | `riscv64/` | RV64IMAC，独立静态库 |
| `xtensa` | `xtensa/` | ESP32 LX6/LX7，PS 中断级 + memw |

外设与 SDK 见 `portable/vendor/`；仿真见 `portable/sim/`；`BM_BACKEND` 兼容包见 `portable/packs/`。

阶段 3 量产 vendor：`esp32_idf`（灯哥平衡车主控板 ESP32-WROOM-32E，`packs/sdk_esp32_idf` = arch/xtensa + vendor）。  
其它 vendor（`nordic_nrf52`、`nxp_kinetis` 等）暂缓，待后续阶段补骨架。

每个 arch 目录提供 `bm_arch_portmacro.h`（`BM_ARCH_YIELD` / `BM_ARCH_DMB` / `BM_ARCH_ALIGN`）；ARM 与 RISC-V 共享头分别位于 `arm/common/`、`riscv/common/`。

`armv7m` / `armv8m_base` 为 INTERFACE 包装，链接时覆盖 `BM_PORT_ARCH_ID`。

移植要点见 [03-Port移植层bm_port.md](../../docs/03-移植与IDE集成/03-Port移植层bm_port.md)。
