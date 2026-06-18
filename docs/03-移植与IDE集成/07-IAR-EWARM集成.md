# 19 IAR-EWARM集成

> **本文职责**：在 IAR EWARM 中添加 Bmelod 源文件、头文件路径与 Port。  
> **集成总览** → [02-挂库到现有工程](02-挂库到现有工程.md)。

## 1. 模型

Port 在应用工程；库以源码或 `.a` 形式加入（同 FreeRTOS 集成习惯）。

## 2. 源文件与 Include

```bash
python tools/list_sources.py --profile event --format iar --root-macro BMELOD_ROOT
python tools/list_sources.py --profile event --list-includes --format iar --root-macro BMELOD_ROOT
```

将 `portable/template/bm_port.c` 复制并改编后**单独加入**工程（不在 list_sources 输出中）。

Include 路径规则同 [06-Keil-MDK集成](06-Keil-MDK集成.md) §2。

## 3. 参考 Port

`portable/vendor/stm32g4/bm_vendor_singleton_stm32g4.c` 展示了 STM32G4 上单例驱动表的接法（临界区由 `arch/armv7em` 提供）。

静态库链接见 [../02-构建与工具链/02-预编译静态库](../02-构建与工具链/02-预编译静态库.md)。
