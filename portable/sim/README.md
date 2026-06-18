# portable/sim — 仿真后端

| 目录 | 说明 |
|------|------|
| `qemu_cm0/` | QEMU micro:bit / nRF 定时器模型 + semihosting UART |
| `qemu_riscv64/` | QEMU `virt` RV64 桩（timer/uart/wdg） |

与 `portable/arch/` 组合后经 `portable/packs/` 导出 `BM_BACKEND` 别名。
