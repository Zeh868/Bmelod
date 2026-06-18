# include/

第三方集成只需将 **`include/`** 加入 Include Path。

## 根目录（对外 API）

| 头文件 | 用途 |
|--------|------|
| `bmelod.h` | **主入口**（按 `bm_config.h` 裁剪） |
| `bm_config.h` | 默认容量与组件开关 |
| `bm_core.h` / `bm_lite.h` / `bm_hybrid.h` / `bm_ultra.h` | 分层聚合 |
| `bm_algorithm.h` | 纯算法库聚合（`BM_ENABLE_ALGORITHM`） |
| `bm_hal.h` | HAL 核心外设聚合（可选） |

## 子目录（实现细节，经聚合头间接包含）

| 目录 | 内容 |
|------|------|
| `bm/common/` | types、log、atomic… |
| `bm/core/` | event、mempool、module… |
| `bm/hybrid/` | hrt、exec、sync、stream… |
| `bm/algorithm/` | PI、滤波、FOC 核、FFT 等纯数学 API |
| `hal/` | `bm_hal_*.h` 应用契约 |
| `drv/` | `bm_drv_*.h`（Port 作者） |

## 推荐用法

```c
#include "bmelod.h"
#include "hal/bm_hal_uart.h"   /* 或 bm_hal.h */
```

Port 工程编译时需额外搜索 `bm/*`、`hal/`、`drv/`（CMake 目标已 PRIVATE 注入；Keil 见 `list_sources.py --list-includes`）。

详见 [docs/01-应用开发/03-bmelod头文件与include.md](../docs/01-应用开发/03-bmelod头文件与include.md)。
