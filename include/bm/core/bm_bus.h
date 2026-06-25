/**
 * @file bm_bus.h
 * @brief bm_bus 统一数据总线门面（Phase 1：LATEST / QUEUE / SIGNAL）
 *
 * 单写多读（SPMC）有界环后端，三种 mode 共用一份借还 API。
 * 编译期用 BM_BUS_DEFINE 静态分配存储，零动态分配。
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-25
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-25       0.1            zeh            Phase 1 初稿
 *
 */
#ifndef BM_BUS_H
#define BM_BUS_H

#include "bm/common/bm_atomic_ipc.h"
#include "bm/common/bm_types.h"
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
    BM_BUS_LATEST = 0,   /**< 最新值，覆盖式，读者不持游标 */
    BM_BUS_QUEUE  = 1,   /**< SPSC 保序队列，单消费者 */
    BM_BUS_SIGNAL = 2,   /**< 多消费者独立游标，保序 */
    BM_BUS_BLOCK  = 3    /**< Phase 2，门面仅声明，LATEST/QUEUE/SIGNAL 调用返回 BM_ERR_NOT_SUPPORTED */
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
 *   - 静态数据缓冲区 `_bm_bus_data_<name>`
 *   - 静态读者游标数组 `_bm_bus_readers_<name>`
 *   - 静态存储控制块 `<name>_storage`（类型 bm_bus_storage_t）
 *
 * @note 编译期仅断言 capacity>=2（所有 mode 通用下界）；LATEST 三缓冲的
 *       capacity>=3 约束（多核防撕裂，spec §7）由运行期 bus_storage_valid /
 *       bm_bus_open 校验拦截（mode 虽是宏参，但跨 mode 复用同一断言更简洁）。
 *
 * @param name          bus 实例名（不带引号，展开为 name##_storage）
 * @param type          元素类型
 * @param capacity      环槽总数（须 >= 2）
 * @param max_consumers 最大读者数
 * @param mode          bm_bus_mode_t 枚举值
 */
#define BM_BUS_DEFINE(name, type, cap_, maxcons_, mode_)                   \
    typedef char _bm_bus_cap_check_##name[((cap_) >= 2u) ? 1 : -1];       \
    static uint8_t _bm_bus_data_##name[(cap_) * sizeof(type)];             \
    static bm_bus_reader_slot_t _bm_bus_readers_##name[(maxcons_)];        \
    static bm_bus_storage_t name##_storage = {                             \
        .write_cur        = BM_ATOMIC_IPC_U32_INIT(0u),                    \
        .latest_published = BM_ATOMIC_IPC_U32_INIT(0u),                    \
        .latest_reading   = BM_ATOMIC_IPC_U32_INIT(BM_BUS_LATEST_NONE),    \
        .latest_writing   = BM_ATOMIC_IPC_U32_INIT(0u),                    \
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
 * @param h bus 句柄指针
 * @return BM_OK 成功；BM_ERR_INVALID 句柄无效
 */
int bm_bus_freeze(bm_bus_t *h);

/**
 * @brief 校验 bus 句柄与 storage 的完整性
 *
 * @param h bus 句柄指针
 * @return BM_OK 通过；BM_ERR_INVALID 未通过
 */
int bm_bus_validate(const bm_bus_t *h);

/**
 * @brief 借出写槽（零拷贝），调用者向 *slot_out 写入数据后调用 commit 或 abort
 *
 * @param h        bus 句柄指针
 * @param slot_out 输出：写槽指针（类型强转后使用）
 * @return BM_OK 成功借到槽；BM_ERR_OVERFLOW QUEUE 模式环满；
 *         BM_ERR_BUSY 重入保护；BM_ERR_INVALID 句柄无效
 */
int bm_bus_acquire_write(bm_bus_t *h, void **slot_out);

/**
 * @brief 提交写操作，发布数据并推进写游标
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
 * @param r 读者句柄指针
 * @return BM_OK 成功；BM_ERR_INVALID 句柄无效或无未完成的 acquire_read
 */
int bm_bus_release(bm_bus_reader_t *r);

/**
 * @brief 查询读者可读的新数据槽数
 *
 * @param r 读者句柄指针
 * @return 可读槽数；r 无效时返回 0
 */
uint32_t bm_bus_ready_count(const bm_bus_reader_t *r);

/**
 * @brief 获取 bus 统计快照
 *
 * @param h   bus 句柄指针
 * @param out 输出统计结构体指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效
 */
int bm_bus_stats(const bm_bus_t *h, bm_bus_stats_t *out);

#endif /* BM_BUS_H */
