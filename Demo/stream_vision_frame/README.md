# stream_vision_frame

## 概述

**DSP 域**视觉帧流示例：合成灰度图像帧中亮块逐帧平移，经 `bm_pipeline` 完成帧差与质心检测，演示 `bm_algo_image` / `bm_algo_vision` 在块流中的用法。

## 层级与成熟度

| 项 | 值 |
|---|---|
| 执行域 | DSP（块流） |
| 成熟度 | E1 |

## 学习重点

- 帧级 `bm_stream` 块交接
- 帧差法运动检测
- 质心坐标跟踪
- `mod_vision_pipeline` 管线编排

## 关键机制与组件

- `bm_stream`、`bm_pipeline`
- `bm_algo_image`、`bm_algo_vision`
- `mod_vision_pipeline`

## 目录结构

```text
stream_vision_frame/
  main.c
  app_vision.h
  bm_config_app.h
  modules/
  CMakeLists.txt
```

## 构建与运行

```powershell
.\tools\demo\run_native.ps1 stream_vision_frame
.\tools\demo\run_qemu.ps1 stream_vision_frame
```

## 验收标准

- 处理帧数 ≥ 30
- 质心跟踪有效（随亮块移动）

## 相关文档

- [01-Demo示例与运行路径](../../docs/01-应用开发/01-Demo示例与运行路径.md)
