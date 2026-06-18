# external_algorithm（K3）

静态绑定外部算法库或协处理器算子，**非**运行时动态加载。

## 描述符

在应用或适配层定义 `bm_ext_algorithm_descriptor_t`：

| 字段 | 说明 |
|------|------|
| `name` | 唯一名称，用于查找与日志 |
| `version_major` / `version_minor` | 算法 ABI 版本 |
| `workspace_bytes` | `init` 所需工作区上限 |
| `init` | 绑定 workspace 与配置，仅 init/重配置阶段 |
| `step` | 单步处理：`input`/`output` 由调用方提供缓冲，`len` 为字节数 |
| `fini` | 释放/复位 workspace，须幂等 |

## 注册

```c
#include "bm/extensions/bm_ext_algorithm.h"

static int my_init(void *ws, uint32_t n, const void *cfg) { ... }
static int my_step(void *ws, const void *in, void *out, uint32_t len) { ... }
static void my_fini(void *ws) { ... }

static const bm_ext_algorithm_descriptor_t g_my_algo = {
    .name = "vendor_dsp_eq",
    .version_major = 1,
    .version_minor = 0,
    .workspace_bytes = 4096,
    .init = my_init,
    .step = my_step,
    .fini = my_fini,
};

void app_register_external_algos(void) {
    (void)bm_ext_algorithm_register(&g_my_algo);
}
```

启动前调用 `bm_ext_algorithm_register`；注册表最多 **8** 项（`BM_EXT_ALGORITHM_MAX`）。

## CMake

根目录启用扩展库：

```cmake
cmake -B build -DBM_ENABLE_EXTENSIONS=ON
```

链接 `bm_extensions`，在 `init` 阶段完成注册与 workspace 大小校验。

## 与 bm_pipeline 的关系

- K3 描述符提供**有界** `step` 回调，适合包装 CMSIS/Codec/TinyML 等外部核。
- 高频路径应通过 `bm_pipeline` 节点或 `bm_exec` 槽位调用 `step`，勿在 SRT 中直接轮询大块数据。
- 完整能力查询（format、WCET）见 [bm_algorithm API](../docs/05-API参考/bm_algorithm.md) §K3。

## 约束

- 所有内存由应用静态分配；`workspace_bytes` 必须在 `init` 前与调用方缓冲一致。
- `step` 默认不得进入 IRQ；若必须，需单独提供 WCET 与可重入证据。
- `abi_major` 不兼容时应拒绝启动（由适配层在 `init` 内检查）。
