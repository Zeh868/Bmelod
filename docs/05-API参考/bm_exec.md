# bm_exec — 确定性执行实例与批量生命周期

头文件：`include/bm/hybrid/bm_exec.h`
实现：`Source/hybrid/bm_exec.c`

## 概述

每个确定性执行单元描述为一个 `bm_exec_t`：包含状态、配置、资源声明、
执行槽和 `ops` 生命周期钩子。控制环、同步采集器及其他有界执行单元共享
同一模型；具体硬件事件由 `bind` 适配器连接。

## 类型

| 类型 | 说明 |
|------|------|
| `bm_exec_run_fn_t` | 单步算法 `void (*)(const bm_exec_t *)` |
| `bm_exec_slot_kind_t` | `HARDWARE`（HAL 事件）或 `PERIODIC`（软件定时） |
| `bm_exec_slot_t` | `kind`、`period_us`、`run`、`bind`、`name` |
| `bm_exec_ops_t` | `init`、`start`、`safe_stop` |
| `bm_exec_t` | `id`、`name`、`state`、`config`、`resources`、`slots`、`claims`、`ops` |

## API

### `bm_exec_init_all(instances, count)`

依次调用各实例 `ops->init`；任一失败则对已初始化实例执行逆序清理。
返回首个失败错误码或 `BM_OK`。

### `bm_exec_start_all(instances, count)`

实例初始化阶段已完成资源冲突检查和 HRT slot 注册；本 API 依次调用
`ops->start`，**全部成功后再** `bm_hrt_start`（若存在 Periodic 槽），HRT 成功后才开放会话门（`STARTED`）。在此之前 Hardware / Periodic `run` 均被忽略。失败时 `exec_abort_session`：关闭会话门、解绑 Hardware、逆序 `safe_stop`。

`instances` 数组元素不得为 NULL，且须与 `init_all` 时指针一致；`slot_count` 不得超过 `BM_CONFIG_MAX_EXEC_SLOTS`。

### `bm_exec_safe_stop_all(instances, count)`

关闭会话门、停止 HRT、解绑 Hardware slot，再对内部注册实例逆序调用
`ops->safe_stop`。传入数组不会被解引用，也不会决定实际停止对象。

### `bm_exec_find(instances, count, id)`

按 `id` 查找实例，未找到返回 `NULL`。

### `bm_exec_get_session()`

返回当前批次会话 `NONE` / `INITED` / `STARTED` / `STOPPING`（只读）。

## 典型模式

```c
static int my_init(const bm_exec_t *inst) { return BM_OK; }
static int my_start(const bm_exec_t *inst) { return BM_OK; }
static void my_safe_stop(const bm_exec_t *inst) { }

static const bm_exec_ops_t g_ops = { my_init, my_start, my_safe_stop };

static const bm_exec_t g_inst = {
    .id = 1,
    .name = "axis0",
    .ops = &g_ops,
    /* slots, claims, state, config ... */
};

static const bm_exec_t *const g_all[] = { &g_inst };

bm_exec_init_all(g_all, 1);
bm_exec_start_all(g_all, 1);
```

## 与资源 / 同步的关系

- 启动前必须通过 `bm_resource_check_conflicts`（在 `init_all` 内自动调用）。
- 多轴相位对齐使用 `bm_sync` 配置同步域，成员指向 `bm_exec_t`。
