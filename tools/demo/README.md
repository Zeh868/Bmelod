# Demo 运行脚本

从仓库根目录或本目录调用均可。**所有示例共享同一 CMake 构建树**（按 variant 一份），框架 `bmelod` 只编译一次：

```text
build/demo/<variant>/              # 统一 CMake 构建根（CMakeCache、bmelod/ 中间产物）
build/demo/<variant>/<example>/   # 各示例可执行产物（*.elf 或 *.exe）
```

`Demo/` 仅保留源码；勿在示例子目录内执行 `cmake -B build`。路径定义见 `demo_paths.sh` / `demo_paths.ps1`（可通过 `BMELOD_DEMO_BUILD_ROOT` 覆盖根目录）。

## variant 说明

| variant | 用途 | 脚本 |
|---------|------|------|
| `native` | PC `native_sim`（21 个示例） | `run_native.ps1` |
| `qemu` | QEMU 冒烟（CI） | `run_qemu.sh` / `run_qemu.ps1` |
| `unix` | Linux/macOS 批量或交互 QEMU | `run_all.sh` / `run_single.sh` |
| `windows` | Windows MinGW 批量或交互 QEMU | `run_all.ps1` / `run_single.ps1` |
| `manual` | 单示例独立调试（旧路径，会重复编译 bmelod） | 见下文 |
| `make` | `qemu_example.mk` / Makefile 示例 | `ultra_blink` 等 |
| `board` | 板级/厂商工程（非脚本路径） | 各示例 `board/` README |

## 统一构建（推荐）

```bash
# QEMU：一次配置，按需编译任意示例
cmake -B build/demo/qemu -S Demo -G "MinGW Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchain-arm-none-eabi.cmake
cmake --build build/demo/qemu --target core_sensor.elf

# PC native_sim（21 个示例，与 QEMU 共用统一树）
cmake -B build/demo/native -S Demo -DBMELOD_DEMO_NATIVE=ON
cmake --build build/demo/native --target core_sensor
```

```powershell
.\tools\demo\run_native.ps1 hrt_servo_stub
.\tools\demo\run_qemu.ps1 interrupt_demo
.\tools\demo\run_all.ps1          # 批量 QEMU
```

## 单示例独立构建（调试用）

仍支持对单个示例目录配置（产物在 `build/demo/manual/<example>/`，会单独编译一份 bmelod）：

```bash
cmake -B build/demo/manual/core_sensor -S Demo/core_sensor \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake
cmake --build build/demo/manual/core_sensor
```

说明见 [docs/01-应用开发/01-Demo示例与运行路径.md](../../docs/01-应用开发/01-Demo示例与运行路径.md)。
