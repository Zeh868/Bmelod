# sim/native — PC 宿主仿真后端

| 内容 | 说明 |
|------|------|
| `bm_sim_singleton_native.c` | timer / uart / wdg |
| 单核临界区 | `packs/native_sim` 链 `arch/host` |
| 外设仿真 | PWM/ADC/COMP/ENC/DMA 等 `bm_drv_*_native.c` |
