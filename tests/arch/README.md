# tests/arch — 架构层交叉编译烟雾测试

在已安装 RISC-V 工具链时验证 `bm_port_arch_riscv32` / `bm_port_arch_riscv64` 可链接。

宿主 PC 烟雾见 `tests/unit/test_arch_port`（`ctest` 内，链接 `arch/stub`）。

## 一键（CI 同款）

```bash
chmod +x tests/arch/run_all.sh
bash tests/arch/run_all.sh
```

## riscv32（CH32 后端 = arch + vendor）

```bash
cmake -B build_arch_rv32 -S tests/arch \
  -DCMAKE_TOOLCHAIN_FILE=../../cmake/toolchain-riscv-none-elf.cmake
cmake --build build_arch_rv32
```

工具链：`riscv-none-elf-gcc` 或 `riscv32-unknown-elf-gcc`（Ubuntu `gcc-riscv32-unknown-elf`）。

## riscv64（QEMU virt 后端）

```bash
cmake -B build_arch_rv64 -S tests/arch \
  -DCMAKE_TOOLCHAIN_FILE=../../cmake/toolchain-riscv64-none-elf.cmake
cmake --build build_arch_rv64
qemu-system-riscv64 -machine virt -cpu rv64 -nographic \
  -kernel build_arch_rv64/arch_smoke_riscv64.elf
```

RV64 固件在 `main` 末尾写 QEMU virt test finisher（`0x100000`），成功时 QEMU 以退出码 0 结束。
