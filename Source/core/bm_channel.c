/**
 * @file bm_channel.c
 * @brief 轻量级 SPSC 数据通道实现
 *
 * 单生产者单消费者环形缓冲，临界区保护索引更新。
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-10       1.1            zeh            SIL-2 防御性校验与查询 API 临界区
 *
 */
#include "bm_channel.h"
#include "bm_critical_wrap.h"
#include "bm/common/bm_atomic_ipc.h"
#include "bm/core/bm_cpu_local.h"
#include "bm_log.h"
#include "bm_safety.h"

#include <string.h>

#if BM_CPU_LOCAL_ENABLE_ROUTE
/*
 * 按 CPU 路由时：生产者/消费者由不同 CPU 使用，索引用原子读写；
 * CHANNEL_LOCK 为空操作——争用由 SPSC 契约保证，无需本核关中断。
 */
static inline uint32_t channel_write_load(const bm_channel_t *ch) {
    return bm_atomic_ipc_load_u32(&ch->write_idx);
}

static inline uint32_t channel_read_load(const bm_channel_t *ch) {
    return bm_atomic_ipc_load_u32(&ch->read_idx);
}

static inline void channel_write_store(bm_channel_t *ch, uint32_t value) {
    bm_atomic_ipc_store_u32(&ch->write_idx, value);
}

static inline void channel_read_store(bm_channel_t *ch, uint32_t value) {
    bm_atomic_ipc_store_u32(&ch->read_idx, value);
}

#define CHANNEL_LOCK(c, s)   do { (void)(c); *(s) = 0u; } while (0)
#define CHANNEL_UNLOCK(c, s) do { (void)(c); (void)(s); } while (0)
#else
/*
 * 默认路径：生产者/消费者运行在同一 CPU 访问域，索引可直接读写；
 * 通过 BM_CRITICAL_ENTER/EXIT 关开临界区，防止中断或抢占破坏索引一致性。
 */
static inline uint32_t channel_write_load(const bm_channel_t *ch) {
    return ch->write_idx;
}

static inline uint32_t channel_read_load(const bm_channel_t *ch) {
    return ch->read_idx;
}

static inline void channel_write_store(bm_channel_t *ch, uint32_t value) {
    ch->write_idx = value;
}

static inline void channel_read_store(bm_channel_t *ch, uint32_t value) {
    ch->read_idx = value;
}

/* 进入临界区：保存当前中断状态并关中断 */
#define CHANNEL_LOCK(c, s)   do { *(s) = BM_CRITICAL_ENTER(); } while (0)
/* 退出临界区：恢复之前保存的中断状态 */
#define CHANNEL_UNLOCK(c, s) BM_CRITICAL_EXIT(s)
#endif

/**
 * @brief 校验通道控制块与缓冲区配置（fail-stop，不修改控制块）
 */
static int channel_validate_config(const bm_channel_t *ch) {
    if (!ch || !ch->buf || ch->elem_size == 0u || ch->capacity < 2U) {
        return BM_ERR_INVALID;
    }
    {
        uint32_t bytes = 0u;
        if (bm_mul_u32_overflow(ch->elem_size, ch->capacity, &bytes)) {
            return BM_ERR_INVALID;
        }
    }
    return BM_OK;
}

/**
 * @brief 校验读写索引在合法范围内（fail-stop）
 */
static int channel_validate_indices(const bm_channel_t *ch) {
    if (!bm_index_in_range_u32(channel_read_load(ch), ch->capacity) ||
        !bm_index_in_range_u32(channel_write_load(ch), ch->capacity)) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

/**
 * @brief 计算槽位字节偏移，溢出则失败
 */
static int channel_slot_offset(const bm_channel_t *ch, uint32_t index,
                               uint32_t *offset_out) {
    uint32_t offset = 0u;

    if (!bm_index_in_range_u32(index, ch->capacity)) {
        return BM_ERR_INVALID;
    }
    if (bm_mul_u32_overflow(index, ch->elem_size, &offset)) {
        return BM_ERR_INVALID;
    }
    *offset_out = offset;
    return BM_OK;
}

/**
 * @brief 在临界区内计算待读取元素数量
 */
static uint32_t channel_count_locked(const bm_channel_t *ch) {
    uint32_t w = channel_write_load(ch);
    uint32_t r = channel_read_load(ch);
    return (w >= r) ? (w - r) : (ch->capacity - r + w);
}

/** 逻辑占用量上界：保留槽区分满/空，最大可存 capacity-1 */
static int channel_logical_count_valid(const bm_channel_t *ch, uint32_t count) {
    return count < ch->capacity ? BM_OK : BM_ERR_INVALID;
}

/**
 * @brief 重置 SPSC 通道读写索引
 *
 * 将读写索引清零，使通道回到空状态。
 *
 * @param ch 通道描述符指针
 */
void bm_channel_reset(bm_channel_t *ch) {
    if (!ch) {
        return;
    }
    bm_irq_state_t s;
    CHANNEL_LOCK(ch, &s);
    channel_write_store(ch, 0u);
    channel_read_store(ch, 0u);
    CHANNEL_UNLOCK(ch, s);
    BM_LOGD("channel", "reset");
}

/**
 * @brief 向通道写入一个元素（生产者侧）
 *
 * @param ch 通道描述符指针
 * @param data 待发送元素数据指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_OVERFLOW 通道已满
 */
int bm_channel_send(bm_channel_t *ch, const void *data) {
    int rc = channel_validate_config(ch);

    if (rc != BM_OK || !data) {
        BM_LOGE("channel", "send invalid channel");
        return BM_ERR_INVALID;
    }
    if (channel_validate_indices(ch) != BM_OK) {
        BM_LOGE("channel", "send corrupt indices");
        return BM_ERR_INVALID;
    }

    bm_irq_state_t s;
    uint32_t write_idx;
    uint32_t read_idx;

    /* 进入临界区后再次校验，防止索引在入锁前被异常破坏 */
    CHANNEL_LOCK(ch, &s);
    if (channel_validate_indices(ch) != BM_OK) {
        CHANNEL_UNLOCK(ch, s);
        return BM_ERR_INVALID;
    }
    if (channel_logical_count_valid(ch, channel_count_locked(ch)) != BM_OK) {
        CHANNEL_UNLOCK(ch, s);
        BM_LOGE("channel", "send corrupt occupancy");
        return BM_ERR_INVALID;
    }

    write_idx = channel_write_load(ch);
    read_idx = channel_read_load(ch);
    uint32_t next = (write_idx + 1u) % ch->capacity;
    if (next == read_idx) {
        /* 保留一槽区分满/空，next==read 表示环已满 */
        CHANNEL_UNLOCK(ch, s);
        BM_LOGW("channel", "send overflow");
        return BM_ERR_OVERFLOW;
    }

    /* 计算当前写槽偏移并拷贝元素 */
    {
        uint32_t offset = 0u;
        if (channel_slot_offset(ch, write_idx, &offset) != BM_OK) {
            CHANNEL_UNLOCK(ch, s);
            return BM_ERR_INVALID;
        }
        memcpy(ch->buf + offset, data, ch->elem_size);
    }

    /* 更新写索引，使新元素对消费者可见 */
    channel_write_store(ch, next);
    CHANNEL_UNLOCK(ch, s);
    return BM_OK;
}

/**
 * @brief 从通道读取一个元素（消费者侧）
 *
 * @param ch 通道描述符指针
 * @param data 接收缓冲区指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_WOULD_BLOCK 通道为空
 */
int bm_channel_recv(bm_channel_t *ch, void *data) {
    int rc = channel_validate_config(ch);

    if (rc != BM_OK || !data) {
        BM_LOGE("channel", "recv invalid channel");
        return BM_ERR_INVALID;
    }
    if (channel_validate_indices(ch) != BM_OK) {
        BM_LOGE("channel", "recv corrupt indices");
        return BM_ERR_INVALID;
    }

    bm_irq_state_t s;
    uint32_t read_idx;
    uint32_t write_idx;

    /* 进入临界区后再次校验索引合法性 */
    CHANNEL_LOCK(ch, &s);
    if (channel_validate_indices(ch) != BM_OK) {
        CHANNEL_UNLOCK(ch, s);
        return BM_ERR_INVALID;
    }
    read_idx = channel_read_load(ch);
    write_idx = channel_write_load(ch);
    if (read_idx == write_idx) {
        /* 读写索引相等表示通道为空 */
        CHANNEL_UNLOCK(ch, s);
        return BM_ERR_WOULD_BLOCK;
    }
    if (channel_logical_count_valid(ch, channel_count_locked(ch)) != BM_OK) {
        CHANNEL_UNLOCK(ch, s);
        BM_LOGE("channel", "recv corrupt occupancy");
        return BM_ERR_INVALID;
    }

    /* 计算当前读槽偏移并拷贝元素到用户缓冲区 */
    {
        uint32_t offset = 0u;
        if (channel_slot_offset(ch, read_idx, &offset) != BM_OK) {
            CHANNEL_UNLOCK(ch, s);
            return BM_ERR_INVALID;
        }
        memcpy(data, ch->buf + offset, ch->elem_size);
    }

    /* 推进读索引，释放该槽位供生产者复用 */
    channel_read_store(ch, (read_idx + 1u) % ch->capacity);
    CHANNEL_UNLOCK(ch, s);
    return BM_OK;
}

/**
 * @brief 查询通道中待读取的元素数量
 *
 * @param ch 通道描述符指针
 * @return 元素数量；无效通道返回 0
 */
uint32_t bm_channel_count(const bm_channel_t *ch) {
    if (channel_validate_config(ch) != BM_OK) {
        return 0u;
    }
    if (channel_validate_indices(ch) != BM_OK) {
        return 0u;
    }

    /* 在临界区内读取双索引，防止并发/中断导致计数错误 */
    bm_irq_state_t s;
    CHANNEL_LOCK(ch, &s);
    uint32_t count = channel_count_locked(ch);

    if (channel_logical_count_valid(ch, count) != BM_OK) {
        CHANNEL_UNLOCK(ch, s);
        return 0u;
    }
    CHANNEL_UNLOCK(ch, s);
    return count;
}

/**
 * @brief 判断通道是否为空
 *
 * @param ch 通道描述符指针
 * @return true 为空或 ch 无效；false 非空
 */
bool bm_channel_is_empty(const bm_channel_t *ch) {
    return bm_channel_count(ch) == 0u;
}

/**
 * @brief 判断通道是否已满
 *
 * @param ch 通道描述符指针
 * @return true 已满或无效；false 仍可写入
 */
bool bm_channel_is_full(const bm_channel_t *ch) {
    if (channel_validate_config(ch) != BM_OK) {
        return true;
    }
    if (channel_validate_indices(ch) != BM_OK) {
        return true;
    }

    /* 在临界区计算下一个写位置，若与读位置重合则判定为满 */
    bm_irq_state_t s;
    CHANNEL_LOCK(ch, &s);
    uint32_t count = channel_count_locked(ch);
    uint32_t next =
        (channel_write_load(ch) + 1u) % ch->capacity;
    bool full = (next == channel_read_load(ch));

    if (channel_logical_count_valid(ch, count) != BM_OK) {
        CHANNEL_UNLOCK(ch, s);
        return true;
    }
    CHANNEL_UNLOCK(ch, s);
    return full;
}
