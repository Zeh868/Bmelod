# portable/

平台 Port 模板与参考后端。目录说明与移植要点见 **[docs/03-移植与IDE集成/01-HAL契约与移植要点.md](../docs/03-移植与IDE集成/01-HAL契约与移植要点.md)**、**[docs/03-移植与IDE集成/03-Port移植层bm_port.md](../docs/03-移植与IDE集成/03-Port移植层bm_port.md)**。

```text
arch/      ISA 临界区与屏障（portable/arch/README.md）
vendor/    芯片外设 + SDK（阶段 3：灯哥平衡车主控板 esp32_idf）
packs/     BM_BACKEND 组合包（优先入口）
sim/       仿真后端（native、qemu_cm0、qemu_riscv64）
template/  量产 bm_port.c 模板
```

阶段 3 当前仅维护 **ESP32-WROOM-32E**（`vendor/esp32_idf` + `packs/sdk_esp32_idf`）。Nordic/NXP 等其它 vendor 暂缓。

挂库步骤见 **[02-挂库到现有工程.md](../docs/03-移植与IDE集成/02-挂库到现有工程.md)**。
