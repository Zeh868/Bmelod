/**
 * @file bm_ipc.c
 * @brief 多核 IPC 协议实现
 *
 * 实现 seq + CRC 单向电平通道、SPSC 可靠命令环、遥测通道及心跳计数器。
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            读游标迁入共享区 reader[cpu]
 * 2026-06-15       1.2            zeh            可靠命令 drain 以 head/tail 为准
 *
 */
#include "bm/core/bm_ipc.h"
#include "bm_config.h"
#include "bm/common/bm_types.h"
#include "bm/common/bm_atomic_ipc.h"
#include "bm/common/bm_crc32.h"
#include "hal/bm_hal_cpu.h"

#include <string.h>

/**
 * @brief 写入带 CRC 的单向通道。
 */
static int write_channel(void *payload_dst,
                         uint32_t *crc_dst,
                         bm_atomic_ipc_u32_t *seq,
                         const void *payload_src,
                         uint32_t payload_len) {
    uint32_t prev = bm_atomic_ipc_load_u32(seq);
    uint32_t next = prev + 1u;
    if ((next & 1u) == 0u) {
        next++;
    }
    bm_atomic_ipc_store_u32(seq, next);
    memcpy(payload_dst, payload_src, payload_len);
    *crc_dst = bm_crc32(payload_dst, payload_len);
    bm_atomic_ipc_fence_release();
    bm_atomic_ipc_store_u32(seq, next + 1u);
    return BM_OK;
}

/**
 * @brief 读取带 CRC 的单向通道。
 */
static int read_channel(const void *payload_src,
                        const uint32_t *crc_src,
                        const bm_atomic_ipc_u32_t *seq,
                        void *payload_dst,
                        uint32_t payload_len,
                        uint32_t *last_seq) {
    uint32_t s = bm_atomic_ipc_load_u32((bm_atomic_ipc_u32_t *)seq);
    if ((s & 1u) != 0u) {
        return BM_ERR_WOULD_BLOCK;
    }
    if (last_seq && s == *last_seq) {
        return BM_ERR_WOULD_BLOCK;
    }
    memcpy(payload_dst, payload_src, payload_len);
    {
        uint32_t crc = bm_crc32(payload_dst, payload_len);
        uint32_t crc_expected = *crc_src;

        bm_atomic_ipc_fence_acquire();
        if (bm_atomic_ipc_load_u32((bm_atomic_ipc_u32_t *)seq) != s) {
            return BM_ERR_WOULD_BLOCK;
        }
        if (crc != crc_expected) {
            return BM_ERR_INVALID;
        }
    }
    if (last_seq) {
        *last_seq = s;
    }
    return BM_OK;
}

int bm_ipc_format(bm_ipc_shared_t *ipc) {
    if (!ipc) {
        return BM_ERR_INVALID;
    }
    memset(ipc, 0, sizeof(*ipc));
    ipc->magic = BM_IPC_MAGIC;
    ipc->layout_version = BM_IPC_LAYOUT_VERSION;
    return BM_OK;
}

int bm_ipc_attach(bm_ipc_shared_t **ipc, uintptr_t base) {
    bm_ipc_shared_t *p;

    if (!ipc) {
        return BM_ERR_INVALID;
    }
    p = (bm_ipc_shared_t *)base;
    if (!p || p->magic != BM_IPC_MAGIC ||
        p->layout_version != BM_IPC_LAYOUT_VERSION) {
        return BM_ERR_INVALID;
    }
    *ipc = p;
    return BM_OK;
}

int bm_ipc_publish_cmd_level(bm_ipc_shared_t *ipc, const bm_ipc_cmd_level_t *cmd) {
    if (!ipc || !cmd) {
        return BM_ERR_INVALID;
    }
    return write_channel(ipc->cmd.payload,
                         &ipc->cmd.crc32,
                         &ipc->cmd.seq,
                         cmd,
                         sizeof(*cmd));
}

int bm_ipc_read_cmd_level_from(bm_ipc_shared_t *ipc,
                               uint8_t source_cpu,
                               bm_ipc_cmd_level_t *out) {
    uint32_t reader = bm_hal_cpu_id();
    uint32_t *last_seq;

    if (!ipc || !out) {
        return BM_ERR_INVALID;
    }
    if (reader >= BM_CONFIG_CPU_COUNT ||
        source_cpu >= BM_CONFIG_CPU_COUNT) {
        return BM_ERR_INVALID;
    }
    last_seq = &ipc->reader[reader].cmd_last_seq[source_cpu];
    return read_channel(ipc->cmd.payload,
                        &ipc->cmd.crc32,
                        &ipc->cmd.seq,
                        out,
                        sizeof(*out),
                        last_seq);
}

int bm_ipc_read_cmd_level(bm_ipc_shared_t *ipc, bm_ipc_cmd_level_t *out) {
#if BM_CONFIG_CPU_COUNT > 1u
    (void)ipc;
    (void)out;
    return BM_ERR_NOT_SUPPORTED;
#else
    return bm_ipc_read_cmd_level_from(ipc, 0u, out);
#endif
}

int bm_ipc_read_telemetry(bm_ipc_shared_t *ipc, bm_ipc_telemetry_t *out) {
#if BM_CONFIG_CPU_COUNT > 1u
    (void)ipc;
    (void)out;
    return BM_ERR_NOT_SUPPORTED;
#else
    return bm_ipc_read_telemetry_from(ipc, 0u, out);
#endif
}

int bm_ipc_publish_telemetry(bm_ipc_shared_t *ipc, const bm_ipc_telemetry_t *tel) {
    if (!ipc || !tel) {
        return BM_ERR_INVALID;
    }
    return write_channel(ipc->tel.payload,
                         &ipc->tel.crc32,
                         &ipc->tel.seq,
                         tel,
                         sizeof(*tel));
}

int bm_ipc_read_telemetry_from(bm_ipc_shared_t *ipc,
                               uint8_t source_cpu,
                               bm_ipc_telemetry_t *out) {
    uint32_t reader = bm_hal_cpu_id();
    uint32_t *last_seq;

    if (!ipc || !out) {
        return BM_ERR_INVALID;
    }
    if (reader >= BM_CONFIG_CPU_COUNT ||
        source_cpu >= BM_CONFIG_CPU_COUNT) {
        return BM_ERR_INVALID;
    }
    last_seq = &ipc->reader[reader].tel_last_seq[source_cpu];
    return read_channel(ipc->tel.payload,
                        &ipc->tel.crc32,
                        &ipc->tel.seq,
                        out,
                        sizeof(*out),
                        last_seq);
}

int bm_ipc_publish_cmd_rel(bm_ipc_shared_t *ipc, const bm_ipc_rel_command_t *cmd) {
#if BM_CONFIG_CPU_COUNT > 1u
    (void)ipc;
    (void)cmd;
    return BM_ERR_NOT_SUPPORTED;
#else
    if (!ipc || !cmd) {
        return BM_ERR_INVALID;
    }
    uint32_t head = bm_atomic_ipc_load_u32(&ipc->rel_cmd.head);
    uint32_t tail = bm_atomic_ipc_load_u32(&ipc->rel_cmd.tail);
    if ((head - tail) >= BM_CONFIG_IPC_REL_CMD_CAPACITY) {
        return BM_ERR_OVERFLOW;
    }
    ipc->rel_cmd.commands[head % BM_CONFIG_IPC_REL_CMD_CAPACITY] = *cmd;
    bm_atomic_ipc_fence_release();
    bm_atomic_ipc_store_u32(&ipc->rel_cmd.head, head + 1u);
    bm_atomic_ipc_inc_u32(&ipc->rel_cmd.count);
    return BM_OK;
#endif
}

int bm_ipc_drain_cmd_rel(bm_ipc_shared_t *ipc,
                         bm_ipc_rel_command_t *out,
                         uint32_t capacity,
                         uint32_t *out_count) {
#if BM_CONFIG_CPU_COUNT > 1u
    (void)ipc;
    (void)out;
    (void)capacity;
    (void)out_count;
    return BM_ERR_NOT_SUPPORTED;
#else
    if (!ipc || (capacity > 0 && !out) || !out_count) {
        return BM_ERR_INVALID;
    }
    *out_count = 0;
    while (*out_count < capacity) {
        uint32_t head = bm_atomic_ipc_load_u32(&ipc->rel_cmd.head);
        uint32_t tail = bm_atomic_ipc_load_u32(&ipc->rel_cmd.tail);
        uint32_t pending = head - tail;
        uint32_t count;

        if (pending == 0u) {
            break;
        }
        if (pending > BM_CONFIG_IPC_REL_CMD_CAPACITY) {
            return (*out_count > 0u) ? BM_OK : BM_ERR_INVALID;
        }
        out[*out_count] =
            ipc->rel_cmd.commands[tail % BM_CONFIG_IPC_REL_CMD_CAPACITY];
        (*out_count)++;
        bm_atomic_ipc_store_u32(&ipc->rel_cmd.tail, tail + 1u);
        count = bm_atomic_ipc_load_u32(&ipc->rel_cmd.count);
        if (count > 0u) {
            bm_atomic_ipc_dec_u32(&ipc->rel_cmd.count);
        }
    }
    return BM_OK;
#endif
}

void bm_ipc_bump_rt_hb(bm_ipc_shared_t *ipc) {
    if (ipc) {
        bm_atomic_ipc_inc_u32(&ipc->rt_hb_seq);
    }
}

void bm_ipc_bump_srt_hb(bm_ipc_shared_t *ipc) {
    if (ipc) {
        bm_atomic_ipc_inc_u32(&ipc->srt_hb_seq);
    }
}

uint32_t bm_ipc_read_rt_hb(const bm_ipc_shared_t *ipc) {
    if (!ipc) {
        return 0;
    }
    return bm_atomic_ipc_load_u32((bm_atomic_ipc_u32_t *)&ipc->rt_hb_seq);
}

uint32_t bm_ipc_read_srt_hb(const bm_ipc_shared_t *ipc) {
    if (!ipc) {
        return 0;
    }
    return bm_atomic_ipc_load_u32((bm_atomic_ipc_u32_t *)&ipc->srt_hb_seq);
}
