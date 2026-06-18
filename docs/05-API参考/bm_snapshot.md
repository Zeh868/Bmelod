# bm_snapshot — 三缓冲无锁快照

头文件：`include/bm_snapshot.h`（header-only）

## 概述

写者发布、读者拷贝，通过 `published` / `reading` 双索引与三份 buffer 避免读写竞争。依赖 `bm_hal_memory.h` 屏障保证可见性。

## 宏

### `BM_SNAPSHOT_DEFINE(name, type)`

定义快照盒类型 `name##_snapshot_t`，内含 `volatile published`、`volatile reading`、`type buffer[3]`。

### `BM_SNAPSHOT_INITIALIZER`

静态初始化：`published = 0`，`reading = BM_SNAPSHOT_NONE`。

### `BM_SNAPSHOT_PUBLISH(box, value_ptr)`

写者侧：选取非 published/reading 的 buffer，写入后 `bm_memory_barrier_release()`，再更新 `published`。

### `BM_SNAPSHOT_READ(box, out_ptr)`

读者侧：自旋直到 `published` 稳定，标记 `reading`，拷贝后清除 `reading`。

## 辅助函数

`bm_snapshot_choose_buffer(published, reading)` — 选取可写 buffer 索引；极端情况下回退到 `0`。

## 使用示例

```c
typedef struct { float x, y; } pose_t;

BM_SNAPSHOT_DEFINE(motor_pose, pose_t);

static motor_pose_snapshot_t g_pose = BM_SNAPSHOT_INITIALIZER;

/* HRT 写者 */
pose_t p = { .x = 1.0f, .y = 2.0f };
BM_SNAPSHOT_PUBLISH(g_pose, &p);

/* 主循环读者 */
pose_t local;
BM_SNAPSHOT_READ(g_pose, &local);
```

## 约束

- 写者与读者可在不同优先级上下文，但 `type` 须可逐字节拷贝（POD）。
- 读者拷贝期间写者仍可向第三块 buffer 写入，不会阻塞 HRT。
- 必须链接平台 `bm_hal_memory` 实现（native / QEMU / MCU 均有参考）。
