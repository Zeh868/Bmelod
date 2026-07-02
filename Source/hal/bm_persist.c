/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_persist.c
 * @brief 键值（KV）持久化存储核心实现（路线图 #10）
 *
 * 维护 RAM KV 表（bm_persist_entry_t 数组），通过 bm_hal_nvs_load()/
 * bm_hal_nvs_save() 与后端存储交互。
 *
 * 分层说明：
 * - KV 逻辑（set/get/erase）始终可用，操作 RAM 表；
 * - init / commit 在有 BM_DRV_HAS_BACKEND 时调用真实 NVS 后端；
 *   无后端时 init 从空表启动，commit 为 no-op（RAM KV 正常，掉电不保存）。
 *
 * 序列化 blob 格式（版本 0x02）：
 * @code
 * [magic 4B]['B','M','K','V'] [version 1B][pad 1B]
 * 每条 entry：
 *   [valid 1B][key (KEY_MAX+1)B][val_len_lo 1B][val_len_hi 1B][val VAL_MAX B]
 * [crc32 4B]（对头部+全部 entry 字节的 CRC32，小端，尾部）
 * @endcode
 *
 * 完整性：尾部 CRC32（bm_crc32，Ethernet 多项式）覆盖 blob 前段（头部+条目）。
 * load 时校验失配即按"格式不识别"路径拒绝加载（保持空表，不 latch 错误）。
 * 格式变更时版本号递增（0x01→0x02），旧版本 blob 经版本不匹配自然拒绝。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-26       1.0            zeh            正式发布（路线图 #10 参数/配置持久化）
 * 2026-07-02       1.1            zeh            blob 尾部增加 CRC32 完整性校验（P1-11），版本 0x01→0x02
 *
 */
#include "bm/common/bm_persist.h"
#include "bm/common/bm_types.h"
#include "bm/common/bm_crc32.h"
#include "bm_config.h"

#ifdef BM_DRV_HAS_BACKEND
#include "hal/bm_hal_nvs.h"
#endif

#include <string.h>

/* -------------------------------------------------------------------------- */
/*  Blob 格式常量                                                               */
/* -------------------------------------------------------------------------- */

/** 序列化头部魔数（4 字节） */
#define PERSIST_MAGIC_0 'B'
#define PERSIST_MAGIC_1 'M'
#define PERSIST_MAGIC_2 'K'
#define PERSIST_MAGIC_3 'V'
/** 当前序列化版本（0x02：尾部新增 CRC32 完整性字段） */
#define PERSIST_VERSION 0x02u
/** 头部大小：magic(4) + version(1) + pad(1) */
#define PERSIST_HEADER_SIZE 6u
/** 尾部 CRC32 字段大小（4 字节，小端） */
#define PERSIST_CRC_SIZE 4u

/**
 * @brief 单条 entry 序列化字节数
 *
 * valid(1) + key(KEY_MAX+1) + val_len_lo(1) + val_len_hi(1) + val(VAL_MAX)
 */
#define PERSIST_ENTRY_SERIAL \
    (1u + (BM_CONFIG_PERSIST_KEY_MAX_LEN + 1u) + 2u + BM_CONFIG_PERSIST_VAL_MAX_LEN)

/** CRC 覆盖的 blob 前段字节数（头部 + 所有条目，不含尾部 CRC） */
#define PERSIST_BODY_SIZE \
    (PERSIST_HEADER_SIZE + \
     (uint16_t)(BM_CONFIG_PERSIST_MAX_ENTRIES) * (uint16_t)(PERSIST_ENTRY_SERIAL))

/** 完整 blob 字节数（头部 + 所有条目 + 尾部 CRC32） */
#define PERSIST_BLOB_SIZE (PERSIST_BODY_SIZE + PERSIST_CRC_SIZE)

/*
 * blob 长度经 uint16_t 域传递给 bm_hal_nvs_load/save。用户加大
 * MAX_ENTRIES / VAL_MAX / KEY_MAX 时须保证不溢出该长度域。
 */
_Static_assert(PERSIST_BLOB_SIZE <= UINT16_MAX,
               "PERSIST_BLOB_SIZE exceeds uint16_t length domain");

/* -------------------------------------------------------------------------- */
/*  RAM KV 表                                                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief 单条 KV 条目（RAM 存储结构）
 */
typedef struct {
    uint8_t  valid;                                       /**< 1=有效，0=空/已删 */
    char     key[BM_CONFIG_PERSIST_KEY_MAX_LEN + 1u];    /**< 键名（null 终止）  */
    uint16_t val_len;                                     /**< 值字节数           */
    uint8_t  val[BM_CONFIG_PERSIST_VAL_MAX_LEN];          /**< 值数据             */
} bm_persist_entry_t;

/** @brief RAM KV 表（全局，BSS 零初始化） */
static bm_persist_entry_t g_store[BM_CONFIG_PERSIST_MAX_ENTRIES];

/** @brief 序列化/反序列化临时缓冲区（BSS 分配，避免大栈使用） */
static uint8_t s_blob[PERSIST_BLOB_SIZE];

/** @brief 初始化完成标志（0=未调用 init） */
static uint8_t g_initialized;

/* -------------------------------------------------------------------------- */
/*  内部辅助                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief 计算 key 长度（C99 兼容，不用 strnlen）
 *
 * 最多检查 max+1 个字符，若 key 在 [0..max] 均无 '\0' 则返回 max+1
 * 表示超长。
 *
 * @param key 待测键名
 * @param max 最大允许长度（BM_CONFIG_PERSIST_KEY_MAX_LEN）
 * @return 实际长度（0..max）或 max+1（超长）
 */
static uint16_t persist_key_len(const char *key, uint16_t max) {
    uint16_t n = 0u;

    while (n <= max && key[n] != '\0') {
        n++;
    }
    return n;
}

/**
 * @brief 将 RAM 表序列化为 blob
 *
 * 按 PERSIST blob 格式逐字节写入 buf，末尾追加覆盖前段的 CRC32（小端）。
 *
 * @param buf 目标缓冲区（长度 >= PERSIST_BLOB_SIZE）
 */
static void persist_serialize(uint8_t *buf) {
    uint16_t i;
    uint32_t crc;
    uint8_t *p = buf;

    /* 头部 */
    *p++ = (uint8_t)PERSIST_MAGIC_0;
    *p++ = (uint8_t)PERSIST_MAGIC_1;
    *p++ = (uint8_t)PERSIST_MAGIC_2;
    *p++ = (uint8_t)PERSIST_MAGIC_3;
    *p++ = (uint8_t)PERSIST_VERSION;
    *p++ = 0u; /* pad */

    /* 条目序列 */
    for (i = 0u; i < (uint16_t)BM_CONFIG_PERSIST_MAX_ENTRIES; i++) {
        *p++ = g_store[i].valid;
        (void)memcpy(p, g_store[i].key, BM_CONFIG_PERSIST_KEY_MAX_LEN + 1u);
        p += BM_CONFIG_PERSIST_KEY_MAX_LEN + 1u;
        *p++ = (uint8_t)(g_store[i].val_len & 0xFFu);
        *p++ = (uint8_t)((g_store[i].val_len >> 8u) & 0xFFu);
        (void)memcpy(p, g_store[i].val, BM_CONFIG_PERSIST_VAL_MAX_LEN);
        p += BM_CONFIG_PERSIST_VAL_MAX_LEN;
    }

    /* 尾部 CRC32：覆盖头部+全部条目字节，小端写入 */
    crc = bm_crc32(buf, (uint32_t)PERSIST_BODY_SIZE);
    *p++ = (uint8_t)(crc & 0xFFu);
    *p++ = (uint8_t)((crc >> 8u) & 0xFFu);
    *p++ = (uint8_t)((crc >> 16u) & 0xFFu);
    *p++ = (uint8_t)((crc >> 24u) & 0xFFu);
}

/**
 * @brief 从 blob 反序列化到 RAM 表
 *
 * 校验头部魔数、版本与尾部 CRC32，通过后逐条还原条目。
 * val_len 越界时裁剪至 VAL_MAX_LEN。
 *
 * @param buf 源缓冲区（长度 >= PERSIST_BLOB_SIZE）
 * @return BM_OK 成功；BM_ERR_INVALID 头部或 CRC 校验失败（视为格式不识别）
 */
static int persist_deserialize(const uint8_t *buf) {
    uint16_t i;
    const uint8_t *p = buf;
    uint16_t vlen;
    uint32_t crc_calc;
    uint32_t crc_stored;
    const uint8_t *crc_p = buf + PERSIST_BODY_SIZE;

    /* 校验魔数和版本 */
    if (p[0] != (uint8_t)PERSIST_MAGIC_0 ||
        p[1] != (uint8_t)PERSIST_MAGIC_1 ||
        p[2] != (uint8_t)PERSIST_MAGIC_2 ||
        p[3] != (uint8_t)PERSIST_MAGIC_3 ||
        p[4] != (uint8_t)PERSIST_VERSION) {
        return BM_ERR_INVALID;
    }

    /* 校验尾部 CRC32：失配按格式不识别处理，拒绝加载 */
    crc_stored = (uint32_t)crc_p[0] |
                 ((uint32_t)crc_p[1] << 8u) |
                 ((uint32_t)crc_p[2] << 16u) |
                 ((uint32_t)crc_p[3] << 24u);
    crc_calc = bm_crc32(buf, (uint32_t)PERSIST_BODY_SIZE);
    if (crc_calc != crc_stored) {
        return BM_ERR_INVALID;
    }
    p += PERSIST_HEADER_SIZE;

    /* 逐条还原 */
    for (i = 0u; i < (uint16_t)BM_CONFIG_PERSIST_MAX_ENTRIES; i++) {
        g_store[i].valid = *p++;

        (void)memcpy(g_store[i].key, p, BM_CONFIG_PERSIST_KEY_MAX_LEN + 1u);
        /* 强制 null 终止，防止损坏数据越界 */
        g_store[i].key[BM_CONFIG_PERSIST_KEY_MAX_LEN] = '\0';
        p += BM_CONFIG_PERSIST_KEY_MAX_LEN + 1u;

        vlen = (uint16_t)(*p) | ((uint16_t)(*(p + 1u)) << 8u);
        p += 2u;

        /* 边界裁剪 */
        if (vlen > (uint16_t)BM_CONFIG_PERSIST_VAL_MAX_LEN) {
            vlen = (uint16_t)BM_CONFIG_PERSIST_VAL_MAX_LEN;
        }
        g_store[i].val_len = vlen;
        (void)memcpy(g_store[i].val, p, BM_CONFIG_PERSIST_VAL_MAX_LEN);
        p += BM_CONFIG_PERSIST_VAL_MAX_LEN;
    }
    return BM_OK;
}

/* -------------------------------------------------------------------------- */
/*  公共 API                                                                    */
/* -------------------------------------------------------------------------- */

int bm_persist_init(void) {
    (void)memset(g_store, 0, sizeof(g_store));
    g_initialized = 1u;

#ifdef BM_DRV_HAS_BACKEND
    {
        int rc = bm_hal_nvs_load(s_blob, (uint16_t)PERSIST_BLOB_SIZE);

        if (rc == BM_OK) {
            /* 忽略反序列化结果：头部损坏时保持空表，属可恢复情况 */
            (void)persist_deserialize(s_blob);
        }
        /* BM_ERR_NOT_FOUND（首次上电无文件）属正常情况，不视为错误 */
    }
#endif /* BM_DRV_HAS_BACKEND */

    return BM_OK;
}

int bm_persist_get(const char *key, void *buf, uint16_t cap, uint16_t *out_len) {
    uint16_t i;

    if (key == NULL || buf == NULL || out_len == NULL) {
        return BM_ERR_INVALID;
    }
    if (!g_initialized) {
        return BM_ERR_NOT_INIT;
    }
    for (i = 0u; i < (uint16_t)BM_CONFIG_PERSIST_MAX_ENTRIES; i++) {
        if (g_store[i].valid &&
            (strncmp(g_store[i].key, key,
                     (uint16_t)BM_CONFIG_PERSIST_KEY_MAX_LEN) == 0)) {
            if (g_store[i].val_len > cap) {
                return BM_ERR_OVERFLOW;
            }
            (void)memcpy(buf, g_store[i].val, g_store[i].val_len);
            *out_len = g_store[i].val_len;
            return BM_OK;
        }
    }
    return BM_ERR_NOT_FOUND;
}

int bm_persist_set(const char *key, const void *data, uint16_t len) {
    uint16_t i;
    uint16_t klen;
    /* 使用哨兵值（最大+1）标记"尚未找到空闲槽" */
    uint16_t free_idx = (uint16_t)BM_CONFIG_PERSIST_MAX_ENTRIES;

    if (key == NULL || data == NULL) {
        return BM_ERR_INVALID;
    }
    if (key[0] == '\0') {
        return BM_ERR_INVALID;
    }
    klen = persist_key_len(key, (uint16_t)BM_CONFIG_PERSIST_KEY_MAX_LEN);
    if (klen > (uint16_t)BM_CONFIG_PERSIST_KEY_MAX_LEN) {
        return BM_ERR_INVALID; /* 键名过长 */
    }
    if (len > (uint16_t)BM_CONFIG_PERSIST_VAL_MAX_LEN) {
        return BM_ERR_OVERFLOW;
    }
    if (!g_initialized) {
        return BM_ERR_NOT_INIT;
    }

    /* 先扫描已有条目（覆盖写） */
    for (i = 0u; i < (uint16_t)BM_CONFIG_PERSIST_MAX_ENTRIES; i++) {
        if (g_store[i].valid &&
            (strncmp(g_store[i].key, key,
                     (uint16_t)BM_CONFIG_PERSIST_KEY_MAX_LEN) == 0)) {
            (void)memcpy(g_store[i].val, data, len);
            g_store[i].val_len = len;
            return BM_OK;
        }
        if (!g_store[i].valid &&
            (free_idx == (uint16_t)BM_CONFIG_PERSIST_MAX_ENTRIES)) {
            free_idx = i;
        }
    }

    /* 新建条目 */
    if (free_idx == (uint16_t)BM_CONFIG_PERSIST_MAX_ENTRIES) {
        return BM_ERR_NO_MEM;
    }
    (void)memset(g_store[free_idx].key, 0, sizeof(g_store[free_idx].key));
    (void)strncpy(g_store[free_idx].key, key, (uint16_t)BM_CONFIG_PERSIST_KEY_MAX_LEN);
    (void)memcpy(g_store[free_idx].val, data, len);
    g_store[free_idx].val_len = len;
    g_store[free_idx].valid   = 1u;
    return BM_OK;
}

int bm_persist_erase(const char *key) {
    uint16_t i;

    if (key == NULL) {
        return BM_ERR_INVALID;
    }
    if (!g_initialized) {
        return BM_ERR_NOT_INIT;
    }
    for (i = 0u; i < (uint16_t)BM_CONFIG_PERSIST_MAX_ENTRIES; i++) {
        if (g_store[i].valid &&
            (strncmp(g_store[i].key, key,
                     (uint16_t)BM_CONFIG_PERSIST_KEY_MAX_LEN) == 0)) {
            g_store[i].valid = 0u;
            return BM_OK;
        }
    }
    return BM_ERR_NOT_FOUND;
}

int bm_persist_commit(void) {
    if (!g_initialized) {
        return BM_ERR_NOT_INIT;
    }

#ifdef BM_DRV_HAS_BACKEND
    persist_serialize(s_blob);
    return bm_hal_nvs_save(s_blob, (uint16_t)PERSIST_BLOB_SIZE);
#else
    /* 无后端：RAM KV 正常，commit 为 no-op */
    return BM_OK;
#endif /* BM_DRV_HAS_BACKEND */
}
