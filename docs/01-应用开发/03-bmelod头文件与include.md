# 20 bmelod头文件与include

> **本文职责**：`bmelod.h` 入口、扁平 `include/` 目录约定与组件开关。  
> **不负责**：CMake 选项细节 → [01-CMake](../02-构建与工具链/01-CMake选项与bm_config.md)。

第三方工程只需将 **`include/`** 加入 Include Path；Port 作者额外需要 `bm/*`、`hal/`、`drv/`（CMake 目标已 PRIVATE 注入）。

---

## 1. 推荐包含方式

```c
#include "bmelod.h"              /* 按 bm_config.h 裁剪的框架入口 */
#include "bm_algorithm.h"        /* 可选：纯算法库，独立于 bm_core */
#include "hal/bm_hal_uart.h"     /* 板级外设契约，不经 bmelod.h 聚合 */
```

---

## 2. 根目录聚合头

| 头文件 | 用途 |
|--------|------|
| `bmelod.h` | **主入口**（读 `BM_CONFIG_ENABLE_*` 拉入 lite/hybrid/ultra） |
| `bm_config.h` | 默认容量与组件开关（应用可同名覆盖） |
| `bm_core.h` / `bm_lite.h` / `bm_hybrid.h` / `bm_ultra.h` | 分层聚合 |
| `bm_algorithm.h` | 纯算法库（`BM_CONFIG_ENABLE_ALGORITHM`） |
| `bm_hal.h` | HAL 核心外设聚合（可选） |

`bmelod.h` 逻辑（简化）：

```text
bm_config.h
  → BM_CONFIG_ENABLE_ULTRA ? bm_ultra.h
  → else bm_lite.h
       → BM_CONFIG_ENABLE_HRT ? bm_hybrid.h
```

---

## 3. 子目录（实现细节）

| 目录 | 内容 |
|------|------|
| `bm/common/` | types、log、atomic、profile_epoch… |
| `bm/core/` | event、mempool、module、channel、wdg… |
| `bm/hybrid/` | hrt、exec、sync、stream、resource… |
| `bm/algorithm/` | `bm_algo_*.h` 纯数学 API |
| `hal/` | `bm_hal_*.h` 应用可见外设契约 |
| `drv/` | `bm_drv_*.h`（Port 作者实现） |

应用代码**不应**依赖 `Source/` 路径；只通过 `include/` 公共 API。

---

## 4. 组件开关（与 CMake 对齐）

`bm_config.h` 中 `BM_CONFIG_ENABLE_*` 与 CMake `BM_ENABLE_*` 由 `bmelod_configure(PROFILE …)` 同步。常见剖面：

| PROFILE | 典型开关 |
|---------|----------|
| `event` / Nano | `MODULE`、`WDG`；混合域关 |
| `lite` | + `CHANNEL`、`SHELL` |
| `control` | + `HRT`、`TICKER`、`EXEC`、`SYNC` |
| 流式 Demo | + `STREAM`、`PIPELINE` |

应用级 `bm_config.h` 放在 **优先于** 框架 `include/` 的搜索路径，或使用 `BM_CONFIG_FILE`（CMake）。

---

## 5. Keil / IAR 手工集成

```bash
python tools/list_sources.py --profile event --format keil
python tools/list_sources.py --list-includes
```

Include Path：**一条** `bmelod-baremetal/include`；勿把 `Source/` 加入应用可见路径。

---

## 6. 相关文档

| 主题 | 文档 |
|------|------|
| CMake 与 PROFILE | [01-CMake](../02-构建与工具链/01-CMake选项与bm_config.md) |
| 挂库步骤 | [02-挂库](../02-构建与工具链/02-挂库到现有工程.md) |
| HAL 契约 | [01-HAL](../03-移植与IDE集成/01-HAL契约与移植要点.md) |
| API 专题 | [api/](../02-构建与工具链/) |
