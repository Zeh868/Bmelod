/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_bus.h
 * @brief bm_bus 统一数据总线门面（LATEST / QUEUE / SIGNAL / BLOCK / IPC）
 *
 * 有界环后端，三种 mode 共用一份借还 API：SIGNAL 为单写多读（SPMC），LATEST/QUEUE 为单读者。
 * BLOCK 模式以控制反转方式委托 bm_block_backend_iface_t 后端（如 bm_stream adapter）实现，
 * IPC 模式以控制反转方式委托 bm_ipc_backend_iface_t 后端（跨核暂存 + CRC + seqlock），
 * 依赖方向保持 hybrid→core，bus 核心不引用任何 hybrid 类型。
 * 编译期用 BM_BUS_DEFINE 静态分配存储，零动态分配。
 * @author zeh (china_qzh@163.com)
 * @version 0.6
 * @date 2026-06-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-25       0.1            zeh            Phase 1 初稿
 * 2026-06-25       0.2            zeh            DET-01 新增 BM_ENABLE_BUS_TEST_HOOK 测试缝声明（LATEST 重试上界验证）
 * 2026-06-26       0.3            zeh            Phase 2 BLOCK 控制反转：bm_bus_bind_block_backend + 专用 produce/consume 入口
 * 2026-06-26       0.4            zeh            新增 bm_bus_reset()：freeze 对称解冻/复位，与 bm_event_reset() 语义对称
 * 2026-06-26       0.5            zeh            seqlock 多读者 LATEST 读：新增 latest_seq + bm_bus_latest_read（增量并存方案）
 * 2026-06-27       0.6            zeh            BM_BUS_IPC 控制反转：bm_bus_bind_ipc_backend + 五入口 IPC 分流
 *
 */
#ifndef BM_BUS_H
#define BM_BUS_H

#include "bm/common/bm_atomic_ipc.h"
#include "bm/common/bm_types.h"
#include "bm/core/bm_block_backend.h"
#include "bm/core/bm_ipc_backend.h"
#include <stdint.h>

/* =========================================================================
 * 运行模式（编译期固定）
 * ========================================================================= */

/**
 * @brief bm_bus 运行模式枚举
 *
 * 每个 bus 实例在 BM_BUS_DEFINE 时确定 mode，运行期不可改。
 */
typedef enum {
    BM_BUS_LATEST = 0,   /**< 最新值，覆盖式，读者不持游标；单读者（SPSC），多消费者请用 SIGNAL */
    BM_BUS_QUEUE  = 1,   /**< SPSC 保序队列，单消费者 */
    BM_BUS_SIGNAL = 2,   /**< 多消费者独立游标，保序 */
    BM_BUS_BLOCK  = 3,   /**< 块流模式，以控制反转委托 bm_block_backend_iface_t 后端（如 bm_stream adapter）；
                              BLOCK 实例不使用 data_buf 三缓冲，存储由后端持有；
                              SPSC 单读者（继承 bm_stream 语义）；需 freeze 前调用
                              bm_bus_bind_block_backend 绑定后端，否则 BLOCK 入口返回 BM_ERR_INVALID */
    BM_BUS_IPC    = 4    /**< IPC 跨核模式，以控制反转委托 bm_ipc_backend_iface_t 后端（暂存 + CRC + seqlock）；
                              存储与游标由后端持有，bus 仅分流 vtable 调用；
                              需 freeze 前调用 bm_bus_bind_ipc_backend 绑定后端，
                              否则 IPC 五入口返回 BM_ERR_INVALID；
                              cap 约束豁免（bus 不感知后端内部容量） */
} bm_bus_mode_t;

/* =========================================================================
 * 类型定义
 * ========================================================================= */

/**
 * @brief 每个读者游标（QUEUE/SIGNAL 专用，LATEST 字段无效）
 */
typedef struct {
    bm_atomic_ipc_u32_t  read_cur;       /**< 该读者已消费到的绝对游标（单调递增，取模 cap 得槽索引） */
    uint32_t             overflow_count; /**< 该读者检出的 overflow 次数 */
    uint8_t              attached;       /**< 1=已 attach，0=空闲 */
    uint8_t              _pad[3];         /**< 对齐填充，保持结构体 4 字节对齐 */
} bm_bus_reader_slot_t;

/**
 * @brief 环后端存储（BM_BUS_DEFINE 展开的 storage 对象类型）
 *
 * 编译期由 BM_BUS_DEFINE 静态实例化，包含数据缓冲区、游标及所有元数据。
 * 运行期通过 bm_bus_open 初始化 owner 等配置后生效。
 */
typedef struct {
    /* --- 写游标（QUEUE/SIGNAL 专用，绝对单调递增，取模 cap 得槽索引；LATEST 不使用） --- */
    bm_atomic_ipc_u32_t  write_cur;        /**< QUEUE/SIGNAL 写游标：绝对单调递增，取模 cap 得槽索引 */
    /* --- LATEST 专属三缓冲槽标记（0..cap-1）；QUEUE/SIGNAL 不使用 --- */
    bm_atomic_ipc_u32_t  latest_published; /**< 已发布槽（读者从此处取最新值） */
    bm_atomic_ipc_u32_t  latest_reading;   /**< 读者正在读的槽；BM_BUS_LATEST_NONE 表示无读者 */
    bm_atomic_ipc_u32_t  latest_writing;   /**< acquire_write 时 choose 选好的写槽（scratch）；
                                              单写者 + write_in_progress 保护，commit 时拷入 latest_published */
    bm_atomic_ipc_u32_t  latest_seq;       /**< seqlock 序列计数（偶=稳定，奇=写进行中）；
                                              bm_bus_latest_read 多观察者拷出读专用；
                                              QUEUE/SIGNAL/BLOCK 不使用 */
    /* --- 元数据（open 时写入，之后只读） --- */
    uint8_t             *data_buf;          /**< 元素数组首地址（指向 storage 内的 data[] 区） */
    uint32_t             elem_size;         /**< 单元素字节数 */
    uint32_t             capacity;          /**< 环槽总数（QUEUE/SIGNAL 实际可用 cap-1，LATEST 等价三缓冲） */
    uint32_t             max_consumers;     /**< 最大读者数（QUEUE 内部强制 1，LATEST 忽略） */
    bm_bus_mode_t        mode;              /**< 运行模式（BM_BUS_DEFINE 时固定，运行期只读） */
    uint8_t              owner_cpu;         /**< 拥有写权限的 CPU 核号（open 时由 cfg 写入） */
    uint8_t              frozen;            /**< freeze 后置 1 */
    volatile uint8_t     write_in_progress; /**< acquire_write 后、commit/abort 前置 1，防重入（volatile：多核空锁/同核 ISR 重入下禁缓存进寄存器） */
    uint8_t              _pad[1];           /**< 对齐填充，使 reader_count 4 字节对齐 */
    uint32_t             reader_count;      /**< 已 attach 读者数 */
    /* --- 读者游标数组（QUEUE/SIGNAL；LATEST 不使用） --- */
    bm_bus_reader_slot_t *readers;          /**< 指向 storage 内的 readers[] 区 */
    /* --- BLOCK 后端（仅 BLOCK 模式有效；bm_bus_bind_block_backend 写入，之后只读） --- */
    const bm_block_backend_iface_t *block_iface; /**< BLOCK 后端 vtable 指针；NULL = 未绑定 */
    void                           *block_ctx;   /**< BLOCK 后端上下文（bm_stream_t * 等） */
    /* --- IPC 后端（仅 IPC 模式有效；bm_bus_bind_ipc_backend 写入，之后只读） --- */
    const bm_ipc_backend_iface_t   *ipc_iface;  /**< IPC 后端 vtable 指针；NULL = 未绑定 */
    void                           *ipc_ctx;    /**< IPC 后端上下文（fifo_stub_t / latest_stub_t 等） */
} bm_bus_storage_t;

/**
 * @brief 运行期句柄（open 后使用）
 */
typedef struct {
    bm_bus_storage_t *storage;
} bm_bus_t;

/**
 * @brief 读者句柄
 */
typedef struct {
    bm_bus_storage_t *storage;
    uint32_t          slot_idx;   /**< readers[] 数组下标；LATEST 时无效（设 UINT32_MAX） */
} bm_bus_reader_t;

/**
 * @brief open 配置
 */
typedef struct {
    uint8_t owner_cpu;   /**< 拥有写权限的 CPU 核号 */
} bm_bus_cfg_t;

/**
 * @brief 统计快照
 */
typedef struct {
    uint32_t write_count;    /**< 成功 commit 次数 */
    uint32_t overflow_count; /**< 写侧检出 overflow 次数（QUEUE 写满）*/
} bm_bus_stats_t;

/* =========================================================================
 * 常量宏
 * ========================================================================= */

/** LATEST 模式下 latest_reading 的"无读者"标记值 */
#define BM_BUS_LATEST_NONE UINT32_MAX

/* =========================================================================
 * 编译期静态存储宏
 * ========================================================================= */

/**
 * @brief 编译期静态定义 bus 存储对象及伴生缓冲区
 *
 * 展开后生成：
 *   - 编译期容量断言（capacity >= 2）
 *   - 编译期 2 的幂断言（QUEUE/SIGNAL 专属，防 2^32 游标回绕静默损坏）
 *   - 静态数据缓冲区 `_bm_bus_data_<name>`
 *   - 静态读者游标数组 `_bm_bus_readers_<name>`
 *   - 静态存储控制块 `<name>_storage`（类型 bm_bus_storage_t）
 *
 * @note 编译期断言两条：(1) capacity>=2（所有 mode 通用下界）；
 *       (2) QUEUE/SIGNAL 的 capacity 须为 2 的幂——自由递增游标取模 cap，
 *       非 2 的幂在 2^32 回绕处取模不连续会致一次静默错读，强制 2 的幂使回绕无缝
 *       （LATEST/BLOCK 不用 write_cur，豁免）。LATEST 三缓冲的 capacity>=3 约束
 *       （多核防撕裂，spec §7）由运行期 bus_storage_valid / bm_bus_open 校验拦截。
 *
 * @param name          bus 实例名（不带引号，展开为 name##_storage）
 * @param type          元素类型
 * @param capacity      环槽总数（须 >= 2）
 * @param max_consumers 最大读者数
 * @param mode          bm_bus_mode_t 枚举值
 */
#define BM_BUS_DEFINE(name, type, cap_, maxcons_, mode_)                   \
    typedef char _bm_bus_cap_check_##name[((cap_) >= 2u) ? 1 : -1];       \
    typedef char _bm_bus_pow2_check_##name[                                \
        ((mode_) == BM_BUS_LATEST || (mode_) == BM_BUS_BLOCK ||            \
         (mode_) == BM_BUS_IPC   ||                                        \
         (((cap_) & ((cap_) - 1u)) == 0u)) ? 1 : -1];                     \
    static uint8_t _bm_bus_data_##name[(cap_) * sizeof(type)];             \
    static bm_bus_reader_slot_t _bm_bus_readers_##name[(maxcons_)];        \
    static bm_bus_storage_t name##_storage = {                             \
        .write_cur        = BM_ATOMIC_IPC_U32_INIT(0u),                    \
        .latest_published = BM_ATOMIC_IPC_U32_INIT(0u),                    \
        .latest_reading   = BM_ATOMIC_IPC_U32_INIT(BM_BUS_LATEST_NONE),    \
        .latest_writing   = BM_ATOMIC_IPC_U32_INIT(0u),                    \
        .latest_seq       = BM_ATOMIC_IPC_U32_INIT(0u),                    \
        .data_buf         = _bm_bus_data_##name,                           \
        .elem_size        = sizeof(type),                                   \
        .capacity         = (uint32_t)(cap_),                              \
        .max_consumers    = (uint32_t)(maxcons_),                          \
        .mode             = (mode_),                                        \
        .owner_cpu        = 0u,                                             \
        .frozen           = 0u,                                             \
        .write_in_progress= 0u,                                             \
        .reader_count     = 0u,                                             \
        .readers          = _bm_bus_readers_##name,                        \
    }

/* =========================================================================
 * 公共 API 声明
 * ========================================================================= */

/**
 * @brief 初始化 bus 句柄并绑定存储对象
 *
 * @param h       bus 句柄指针（调用者分配）
 * @param storage 由 BM_BUS_DEFINE 展开的 storage 对象指针
 * @param cfg     open 配置（owner_cpu 等）
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效或存储未通过校验
 */
int bm_bus_open(bm_bus_t *h, bm_bus_storage_t *storage, const bm_bus_cfg_t *cfg);

/**
 * @brief 关闭 bus 句柄，解除与 storage 的绑定
 *
 * @param h bus 句柄指针
 * @return BM_OK 成功；BM_ERR_INVALID 句柄无效
 */
int bm_bus_close(bm_bus_t *h);

/**
 * @brief 冻结 bus 拓扑，freeze 后 reader_attach 将返回 BM_ERR_BUSY
 *
 * @par 多核契约
 * `frozen` 为普通 uint8_t，跨核可见性依赖上层串行契约（见 reader_attach 约束）。
 * `reader_attach` 与 `freeze` 仅允许在 freeze 之前、由单一协调流程**串行**调用；
 * 框架不强制此契约。SIGNAL 跨核多消费者各核登记须由上层串行化（如在 boot 屏障内）。
 *
 * @param h bus 句柄指针
 * @return BM_OK 成功；BM_ERR_INVALID 句柄无效
 */
int bm_bus_freeze(bm_bus_t *h);

/**
 * @brief 将 bus storage 运行期状态复位到 open 后 pristine 并解冻（frozen=0）
 *
 * 与 bm_event_reset() 语义对称：解冻同时清零所有运行期游标、计数与读者槽，
 * 使 bus 恢复到 open 刚完成后的干净状态，可重新执行 reader_attach/freeze 流程。
 *
 * 复位内容（运行期状态）：
 *   - write_cur=0
 *   - latest_published=BM_BUS_LATEST_NONE，latest_reading=BM_BUS_LATEST_NONE，latest_writing=0
 *   - latest_seq=0（seqlock 序列计数归零，偶=稳定初态）
 *   - write_in_progress=0
 *   - reader_count=0
 *   - 每个 reader slot：read_cur=0、overflow_count=0、attached=0
 *   - frozen=0（解冻，reader_attach 可再次接受）
 *
 * 保留不变（编译期/open 配置）：
 *   - mode、capacity、elem_size、max_consumers、owner_cpu
 *   - data_buf、readers 指针
 *   - BLOCK 模式的 block_iface/block_ctx 绑定
 *
 * @par 多核契约
 * reset 仅允许在安全相位（单一协调流程、无并发产消、稳态之前或恢复期）调用，
 * 不得在多核产消并发期间调用。此契约与 freeze/reader_attach 串行契约同构；
 * 框架不强制，由上层保证。
 *
 * @par BLOCK 后端
 * reset 仅复位 bus core 自身运行期状态，不自动复位后端（bm_stream 等）；
 * 后端有独立生命周期，调用方需自行复位后端。
 *
 * @par 幂等
 * 连续多次 reset 无副作用。
 *
 * @param h bus 句柄指针
 * @return BM_OK 成功；BM_ERR_INVALID h 或 h->storage 为空
 */
int bm_bus_reset(bm_bus_t *h);

/**
 * @brief 校验 bus 句柄与 storage 的完整性
 *
 * 实际执行的约束（与 bus_storage_valid 一致）：
 *   - cap >= 2（所有 mode 通用下界）
 *   - mode 合法（LATEST/QUEUE/SIGNAL/BLOCK）
 *   - LATEST：cap >= 3（三缓冲多核防撕裂）
 *   - QUEUE：max_consumers == 1（唯一消费者语义）
 *
 * @param h bus 句柄指针
 * @return BM_OK 通过；BM_ERR_INVALID 未通过
 */
int bm_bus_validate(const bm_bus_t *h);

/**
 * @brief 借出写槽（零拷贝），调用者向 *slot_out 写入数据后调用 commit 或 abort
 *
 * @par 多核契约（owner-only，bm_cpu_is_owner 强制）
 * bus 为单写者 SPMC：写路径（acquire_write/commit/abort）**仅允许 owner_cpu 调用**。
 * 多核下重入保护由 `write_in_progress`（volatile，非原子 RMW）承担，其正确性依赖
 * "同一时刻只有 owner 核进入写路径"这一上层契约——多核下由 **bm_cpu_is_owner()** 守卫
 * 拒绝非 owner 核进入写路径（返回 BM_ERR_INVALID）；单核编译为 no-op（零开销）。
 *
 * @param h        bus 句柄指针
 * @param slot_out 输出：写槽指针（类型强转后使用）
 * @return BM_OK 成功借到槽；BM_ERR_OVERFLOW QUEUE 模式环满；
 *         BM_ERR_BUSY 重入保护；BM_ERR_INVALID 句柄无效；
 *         BM_ERR_NOT_SUPPORTED BLOCK 模式（BLOCK 使用专用 produce/consume 入口）
 */
int bm_bus_acquire_write(bm_bus_t *h, void **slot_out);

/**
 * @brief 提交写操作，发布数据并推进写游标
 *
 * @note owner-only：与 acquire_write 同属写路径，仅允许 owner_cpu 调用（多核下
 *       由 bm_cpu_is_owner() 守卫强制，单核 no-op；详见 bm_bus_acquire_write 契约）。
 *
 * @param h bus 句柄指针
 * @return BM_OK 成功；BM_ERR_INVALID 句柄无效或无未完成的 acquire_write
 */
int bm_bus_commit(bm_bus_t *h);

/**
 * @brief 放弃当前 acquire_write，不发布数据
 *
 * @param h bus 句柄指针
 * @return BM_OK 成功；BM_ERR_INVALID 句柄无效或无未完成的 acquire_write
 */
int bm_bus_abort(bm_bus_t *h);

/**
 * @brief 登记读者，分配追赶游标（QUEUE/SIGNAL）或标记（LATEST）
 *
 * @par 多核契约
 * 多核下 `reader_attach` 仅允许在 freeze 之前、由单一协调流程**串行**调用；
 * 框架不强制此契约。SIGNAL 跨核多消费者各核登记须由上层串行化（如在 boot 屏障内）。
 *
 * @param h bus 句柄指针
 * @param r 读者句柄指针（调用者分配）
 * @return BM_OK 成功；BM_ERR_BUSY 已 freeze；
 *         BM_ERR_INVALID 参数无效、超过 max_consumers 或违反 QUEUE 唯一消费者约束
 */
int bm_bus_reader_attach(bm_bus_t *h, bm_bus_reader_t *r);

/**
 * @brief 借出读槽（零拷贝），调用者读取 *slot_out 后调用 release
 *
 * @param r        读者句柄指针
 * @param slot_out 输出：读槽指针（只读）
 * @return BM_OK 成功借到槽；BM_ERR_WOULD_BLOCK 无新数据；
 *         BM_ERR_OVERFLOW 读者被绕过（游标已跳最旧可用槽）；
 *         BM_ERR_INVALID 句柄无效
 */
int bm_bus_acquire_read(bm_bus_reader_t *r, const void **slot_out);

/**
 * @brief 归还读槽，解除借阅标记
 *
 * @par 返回码差异（mode 相关）
 * - **LATEST**：恒返回 BM_OK，幂等（清 latest_reading=BM_BUS_LATEST_NONE，无论是否持有 acquire）。
 * - **QUEUE**：推进读者游标，恒返回 BM_OK（写者满即拒，消费窗口不会被覆盖）。
 * - **SIGNAL**：推进读者游标前**复检消费窗口**——借出后写者若覆盖式绕过本槽
 *   （(write_cur-read_cur)>=cap），本帧已撕裂，返回 **BM_ERR_OVERFLOW** 并把游标跳到
 *   最旧可用槽；调用方须**作废本帧、重新 acquire**。这是零拷贝借用窗口的确定性保护，
 *   与 acquire_read 的 lap 检测对称（acquire 挡读前被绕过，release 挡读中被绕过）。
 *
 * @param r 读者句柄指针
 * @return BM_OK 成功；BM_ERR_OVERFLOW SIGNAL 消费窗口内本帧被覆盖（作废重取）；
 *         BM_ERR_INVALID 句柄无效（QUEUE/SIGNAL slot_idx 越界）
 */
int bm_bus_release(bm_bus_reader_t *r);

/**
 * @brief 查询读者可读的新数据槽数
 *
 * @note 多核下返回值为**瞬时估计**、非精确快照：连读多个原子量做差，
 *       写者并发推进时结果可能偏大，已裁剪至 cap-1 不越界。
 *       仅用于启发式判断，不可作为精确计数依据。
 *
 * @param r 读者句柄指针
 * @return 可读槽数；r 无效时返回 0
 */
uint32_t bm_bus_ready_count(const bm_bus_reader_t *r);

/**
 * @brief 获取 bus 统计快照
 *
 * @note 多核下为**瞬时估计**、非精确快照：连读多个原子量做差，
 *       写者并发推进时可能偏大，已裁剪至 cap-1 不越界。
 *
 * @param h   bus 句柄指针
 * @param out 输出统计结构体指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效
 */
int bm_bus_stats(const bm_bus_t *h, bm_bus_stats_t *out);

/* =========================================================================
 * BLOCK 模式专用 API
 *
 * @par BLOCK 同核 SPSC 契约（owner-only）
 * BLOCK 模式经 bm_bus_bind_block_backend 委托给块流后端（如 hybrid 层 bm_stream），
 * 该后端是**同核 SPSC**：生产路径（block_produce_acquire/commit/abort）与消费路径
 * （block_consume_acquire/release）**必须由同一 owner_cpu 调用**。
 * 跨核消费（生产在 A 核、消费在 B 核）属**未定义/非法用法**——后端（bm_stream）以
 * **bm_cpu_is_owner()** 统一守卫（多核强制，单核 no-op），越权核调用将被拒（返回
 * BM_ERR_INVALID），且其内部状态机的 SPSC 无锁正确性本就依赖"同一核串行进出产消路径"
 * 这一前提。bus core 层不再额外守卫 BLOCK 入口，避免与后端守卫形成双重拒绝与双重日志；
 * 同核约束由后端 bm_stream 的 bm_cpu_is_owner() 守卫直接保证。
 * 经 QEMU -smp 2 实测验证（见
 * docs/.../evidence/2026-06-26-bm-bus-block-ioc-verification.md）。
 * ========================================================================= */

/**
 * @brief 绑定 BLOCK 后端（仅 BLOCK 模式合法，必须在 freeze 之前调用）
 *
 * 将函数指针表 iface 与上下文 ctx 写入 storage，之后 BLOCK 入口透传至 vtable。
 * 重复 bind（未 freeze 前）将覆盖旧绑定。freeze 后调用返回 BM_ERR_BUSY。
 * 非 BLOCK 模式调用返回 BM_ERR_NOT_SUPPORTED。
 *
 * @par 多核契约
 * bind 须在 freeze 之前、由单一协调流程串行调用；与 reader_attach 串行契约同构。
 * 绑定后的产消须遵守 BLOCK 同核 SPSC 契约（见本段 API 区段头）：produce 与 consume
 * 必须由同一 owner_cpu 调用，跨核为非法用法。
 *
 * @param h     bus 句柄指针
 * @param iface 后端 vtable 指针（生命期须覆盖 bus 整个运行期）
 * @param ctx   后端上下文（如 bm_stream_t *，传递给 vtable 各函数）
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法；BM_ERR_BUSY 已 freeze；
 *         BM_ERR_NOT_SUPPORTED 非 BLOCK 模式
 */
int bm_bus_bind_block_backend(bm_bus_t *h,
                               const bm_block_backend_iface_t *iface,
                               void *ctx);

/**
 * @brief BLOCK 生产者：借出空闲块
 *
 * 仅 BLOCK 模式合法；透传至已绑定后端的 producer_acquire。
 * 未绑定后端时返回 BM_ERR_INVALID。
 *
 * @note owner-only：生产路径仅允许 owner_cpu 调用（BLOCK 同核 SPSC 契约，见 API 区段头）；
 *       多核下由后端（bm_stream）bm_cpu_is_owner() 强制守卫，单核 no-op。
 *
 * @param h         bus 句柄指针
 * @param block_out 输出：不透明 block 指针
 * @return BM_OK 成功；BM_ERR_OVERFLOW 无空闲块；BM_ERR_INVALID 未绑定或参数非法；
 *         BM_ERR_NOT_SUPPORTED 非 BLOCK 模式
 */
int bm_bus_block_produce_acquire(bm_bus_t *h, void **block_out);

/**
 * @brief BLOCK 生产者：提交块（发布数据）
 *
 * 透传 valid_bytes 与 ts_ns 至后端 producer_commit。
 *
 * @note owner-only：生产路径仅允许 owner_cpu 调用（BLOCK 同核 SPSC 契约，见 API 区段头）；
 *       多核下由后端（bm_stream）bm_cpu_is_owner() 强制守卫，单核 no-op。
 *
 * @param h           bus 句柄指针
 * @param block       由 bm_bus_block_produce_acquire 借出的块
 * @param valid_bytes 有效数据字节数
 * @param ts_ns       时间戳（纳秒），0 表示无时间戳
 * @return BM_OK 成功；BM_ERR_INVALID 未绑定或参数非法；BM_ERR_NOT_SUPPORTED 非 BLOCK 模式
 */
int bm_bus_block_produce_commit(bm_bus_t *h, void *block,
                                uint32_t valid_bytes, uint64_t ts_ns);

/**
 * @brief BLOCK 生产者：放弃已借块（不发布）
 *
 * @note owner-only：生产路径仅允许 owner_cpu 调用（BLOCK 同核 SPSC 契约，见 API 区段头）；
 *       多核下由后端（bm_stream）bm_cpu_is_owner() 强制守卫，单核 no-op。
 *
 * @param h     bus 句柄指针
 * @param block 由 bm_bus_block_produce_acquire 借出的块
 * @return BM_OK 成功；BM_ERR_INVALID 未绑定或参数非法；BM_ERR_NOT_SUPPORTED 非 BLOCK 模式
 */
int bm_bus_block_produce_abort(bm_bus_t *h, void *block);

/**
 * @brief BLOCK 消费者：借出最旧 READY 块
 *
 * @note owner-only：消费路径仅允许 owner_cpu 调用（须与生产同核，BLOCK 同核 SPSC
 *       契约，见 API 区段头）；多核下由后端（bm_stream）bm_cpu_is_owner() 强制守卫
 *       拒绝越权核（BM_ERR_INVALID），单核 no-op。
 *
 * @param h         bus 句柄指针
 * @param block_out 输出：不透明 block 指针
 * @return BM_OK 成功；BM_ERR_WOULD_BLOCK 无 READY 块；
 *         BM_ERR_INVALID 未绑定或参数非法；BM_ERR_NOT_SUPPORTED 非 BLOCK 模式
 */
int bm_bus_block_consume_acquire(bm_bus_t *h, void **block_out);

/**
 * @brief BLOCK 消费者：归还块
 *
 * @note owner-only：消费路径仅允许 owner_cpu 调用（须与生产同核，BLOCK 同核 SPSC
 *       契约，见 API 区段头）；多核下由后端（bm_stream）bm_cpu_is_owner() 强制守卫，单核 no-op。
 *
 * @param h     bus 句柄指针
 * @param block 由 bm_bus_block_consume_acquire 借出的块
 * @return BM_OK 成功；BM_ERR_INVALID 未绑定或参数非法；BM_ERR_NOT_SUPPORTED 非 BLOCK 模式
 */
int bm_bus_block_consume_release(bm_bus_t *h, void *block);

/* =========================================================================
 * IPC 模式专用 API
 * ========================================================================= */

/**
 * @brief 绑定 IPC 后端（仅 IPC 模式合法，必须在 freeze 之前调用）
 *
 * 将 vtable 指针与上下文写入 storage->ipc_iface/ipc_ctx。freeze 后拒绝绑定。
 * 重复 bind（未 freeze 前）覆盖旧绑定。freeze 后调用返回 BM_ERR_BUSY。
 * 非 IPC 模式调用返回 BM_ERR_NOT_SUPPORTED。
 *
 * @par 多核契约
 * bind 须在 freeze 之前、由单一协调流程串行调用；与 reader_attach 串行契约同构。
 *
 * @param h     bus 句柄指针
 * @param iface IPC 后端 vtable 指针（生命期须覆盖 bus 整个运行期）
 * @param ctx   IPC 后端上下文（如 fifo_stub_t *，传递给 vtable 各函数）
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法；BM_ERR_BUSY 已 freeze；
 *         BM_ERR_NOT_SUPPORTED 非 IPC 模式
 */
int bm_bus_bind_ipc_backend(bm_bus_t *h,
                              const bm_ipc_backend_iface_t *iface,
                              void *ctx);

/**
 * @brief LATEST 多观察者拷出式读：将最新发布值 memcpy 到调用者缓冲区
 *
 * 基于 seqlock（latest_seq）实现，允许任意数量观察者并发调用，各自拷出最新值；
 * 无需 reader_attach、无需游标，不影响 latest_reading。
 *
 * @par 与单读者零拷贝路径的关系
 * bm_bus_acquire_read / bm_bus_release（单读者零拷贝借还）与本函数互不干扰，可并存：
 * - 单读者路径通过 latest_reading 防写者 choose 覆盖正读槽；
 * - 多观察者拷出路径通过 seqlock 保证拷出一致性，不持 latest_reading。
 * 同一 bus 混用两种读法的语义边界：若单读者正持有某槽（latest_reading != NONE），
 * 多观察者仍可并发拷出最新发布值（latest_published 指向的槽），二者指向的槽可能相同
 * 或不同，但各自一致性均由各自保护机制（latest_reading / seqlock）独立保障。
 *
 * @par 确定性约束（DET-01 seqlock 扩展）
 * 非阻塞、有界重试（上界 BM_CONFIG_BUS_LATEST_MAX_RETRIES），WCET 可静态分析；
 * 零动态分配；单写者契约不变。
 *
 * @par 多核可见性
 * 读侧以 acquire 序读 latest_seq 与 latest_published；
 * 写侧（bm_bus_commit LATEST 路径）以 release 序维护 seqlock 屏障（奇=写进行中，偶=稳定）。
 *
 * @param h   bus 句柄指针（不修改 bus 运行期状态）
 * @param dst 目标缓冲区（调用者分配，大小须 >= elem_size）
 * @return BM_OK 成功拷出最新值；
 *         BM_ERR_WOULD_BLOCK 尚无发布值（latest_published == BM_BUS_LATEST_NONE）
 *           或重试耗尽（写者持续发布致 seqlock 始终失稳）；
 *         BM_ERR_NOT_SUPPORTED 非 LATEST 模式；
 *         BM_ERR_INVALID h、h->storage 或 dst 为空
 */
int bm_bus_latest_read(const bm_bus_t *h, void *dst);

#ifdef BM_ENABLE_BUS_TEST_HOOK
/**
 * @brief 测试钩子：LATEST acquire_read 每次重试迭代调用一次
 *
 * 仅在 BM_ENABLE_BUS_TEST_HOOK 下编入；生产构建零开销。测试可注入函数
 * 模拟写者在读窗口持续发布，强制 spin-until-stable 失稳，从而覆盖并验证
 * 重试上界（DET-01）：超 BM_CONFIG_BUS_LATEST_MAX_RETRIES 次后非阻塞返回。
 */
extern void (*bm_bus_test_latest_read_hook)(bm_bus_storage_t *st);

/**
 * @brief 测试钩子：bm_bus_latest_read seqlock 拷贝完成后、seq2 复读前调用一次
 *
 * 仅在 BM_ENABLE_BUS_TEST_HOOK 下编入；生产构建零开销。测试可注入函数
 * 模拟写者在拷贝窗口修改 seq，强制 seqlock 始终失稳，从而覆盖并验证
 * 重试上界（DET-01 seqlock 扩展）：超 BM_CONFIG_BUS_LATEST_MAX_RETRIES 次后
 * 非阻塞返回 BM_ERR_WOULD_BLOCK。
 */
extern void (*bm_bus_test_latest_multi_read_hook)(bm_bus_storage_t *st);
#endif

#ifdef BM_BUS_ALLOW_INTERNAL
/**
 * @brief LATEST 拷出并回传本次 seqlock 校验通过的稳定序号（内部只读 seq 访问器，仅门面库可见，公共 API 零增长）
 *
 * 与 bm_bus_latest_read 同一 seqlock 循环，额外经 out_seq 回传 seq2==seq1 的稳定序号；
 * seq 与拷到的值来自同一次校验，无 TOCTOU。供 bm_tt_schedule seq-delta 判龄使用。
 * 仅在定义 BM_BUS_ALLOW_INTERNAL 的翻译单元可见（照 BM_ENABLE_BUS_TEST_HOOK 弱强制模式）。
 *
 * @param h        bus 句柄（LATEST 模式）
 * @param dst      目标缓冲，大小须 >= elem_size
 * @param out_seq  回传稳定序号（偶数）；仅 BM_OK 时有效
 * @return 同 bm_bus_latest_read
 */
int bm_bus_latest_read_seq(const bm_bus_t *h, void *dst, uint32_t *out_seq);
#endif /* BM_BUS_ALLOW_INTERNAL */

#endif /* BM_BUS_H */
