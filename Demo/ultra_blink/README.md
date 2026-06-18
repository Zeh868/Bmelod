# ultra_blink

## 概述

Bmelod **Ultra 域**最简示例：不依赖 `bm_module`，通过头文件级事件队列完成定时 tick 与按钮事件分发，并驱动 LED 状态翻转。

## 层级与成熟度

| 项 | 值 |
|---|---|
| 执行域 | Ultra |
| 成熟度 | D0（机制演示） |

## 学习重点

- `bm_ultra_publish` / `bm_ultra_process` 事件循环
- `BM_ULTRA_CALLBACK_TABLE_DEFINE` 回调表注册
- 最小化依赖下的日志与 UART 输出

## 关键机制与组件

- `bm_ultra`：极简事件队列
- `example_support`：示例用延时与打印

## 目录结构

```text
ultra_blink/
  main.c          # 主循环：发布 tick / 按钮事件并处理
  bm_config_app.h     # 框架裁剪配置
  CMakeLists.txt
```

## 构建与运行

本示例**仅支持 QEMU**（无 `native_sim` 目标）。

```powershell
# 仓库根目录
.\tools\demo\run_qemu.ps1 ultra_blink
```

```bash
./tools/demo/run_qemu.sh ultra_blink
```

构建产物：`build/demo/<variant>/ultra_blink/ultra_blink.elf`

## 验收标准

- 串口输出 `EXAMPLE_ULTRA: PASS`
- tick 计数 ≥ 3 后 LED 交替翻转日志可见

## 相关文档

- [01-Demo示例与运行路径](../../docs/01-应用开发/01-Demo示例与运行路径.md)
- [tools/demo/README.md](../../tools/demo/README.md)
