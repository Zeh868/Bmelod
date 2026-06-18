# tests/qemu — QEMU 冒烟测试

## Cortex-M0（ARM 工具链）

```bash
cmake -B build_qemu -S tests/qemu \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake
cmake --build build_qemu
cmake --build build_qemu --target run-smoke-cm0
```

单核架构层冒烟见 [`tests/arch/`](../arch/README.md)。

按 CPU 路由的 QEMU 测试请查看对应的集成仓。
