/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_nvs_dual_slot.c
 * @brief NVS 双槽格式解析/封装实现
 * @maturity E1
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增双槽 NVS 格式辅助
 *
 * 2026-08-01       1.0            Codex            补全中文 Doxygen 合规注释
 */
#include "bm_nvs_dual_slot.h"

#include "bm/common/bm_crc32.h"
#include "bm/common/bm_types.h"

#include <string.h>

uint32_t bm_nvs_slot_min_size(uint16_t payload_len) {
    uint32_t need = (uint32_t)BM_NVS_SLOT_OVERHEAD + (uint32_t)payload_len;

    if (need < (uint32_t)payload_len) {
        return 0u; /* 溢出 */
    }
    return need;
}

/**
 * @brief 读小端 u32
 */
static uint32_t bm_nvs_rd_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/**
 * @brief 写小端 u32
 */
static void bm_nvs_wr_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

/**
 * @brief 读小端 u16
 */
static uint16_t bm_nvs_rd_u16_le(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/**
 * @brief 写小端 u16
 */
static void bm_nvs_wr_u16_le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

/**
 * @brief 判断槽是否为擦除态（全 0xFF）
 */
static int bm_nvs_slot_is_erased(const uint8_t *slot, uint32_t slot_size) {
    uint32_t i;

    for (i = 0u; i < slot_size; ++i) {
        if (slot[i] != 0xFFu) {
            return 0;
        }
    }
    return 1;
}

int bm_nvs_slot_parse(const uint8_t *slot, uint32_t slot_size,
                      uint16_t expect_len, uint32_t *out_seq,
                      const uint8_t **out_payload) {
    uint32_t need;
    uint16_t plen;
    uint32_t crc_stored;
    uint32_t crc_calc;

    if (slot == NULL || slot_size < BM_NVS_SLOT_OVERHEAD) {
        return BM_ERR_INVALID;
    }
    if (bm_nvs_slot_is_erased(slot, slot_size) != 0) {
        return BM_ERR_NOT_FOUND;
    }

    if (slot[0] != BM_NVS_SLOT_MAGIC0 || slot[1] != BM_NVS_SLOT_MAGIC1 ||
        slot[2] != BM_NVS_SLOT_MAGIC2 || slot[3] != BM_NVS_SLOT_MAGIC3) {
        return BM_ERR_INVALID;
    }
    if (slot[4] != BM_NVS_SLOT_VERSION) {
        return BM_ERR_INVALID;
    }

    plen = bm_nvs_rd_u16_le(&slot[10]);
    if (plen != expect_len) {
        return BM_ERR_INVALID;
    }
    need = bm_nvs_slot_min_size(plen);
    if (need == 0u || need > slot_size) {
        return BM_ERR_INVALID;
    }

    crc_stored = bm_nvs_rd_u32_le(&slot[BM_NVS_SLOT_HDR_SIZE + plen]);
    crc_calc = bm_crc32(slot, BM_NVS_SLOT_HDR_SIZE + (uint32_t)plen);
    if (crc_stored != crc_calc) {
        return BM_ERR_INVALID;
    }

    if (out_seq != NULL) {
        *out_seq = bm_nvs_rd_u32_le(&slot[6]);
    }
    if (out_payload != NULL) {
        *out_payload = &slot[BM_NVS_SLOT_HDR_SIZE];
    }
    return BM_OK;
}

int bm_nvs_slot_pack(uint8_t *slot, uint32_t slot_size, uint32_t seq,
                     const uint8_t *payload, uint16_t payload_len) {
    uint32_t need;
    uint32_t crc;

    if (slot == NULL || payload == NULL) {
        return BM_ERR_INVALID;
    }
    need = bm_nvs_slot_min_size(payload_len);
    if (need == 0u || need > slot_size) {
        return BM_ERR_OVERFLOW;
    }

    slot[0] = BM_NVS_SLOT_MAGIC0;
    slot[1] = BM_NVS_SLOT_MAGIC1;
    slot[2] = BM_NVS_SLOT_MAGIC2;
    slot[3] = BM_NVS_SLOT_MAGIC3;
    slot[4] = BM_NVS_SLOT_VERSION;
    slot[5] = 0u;
    bm_nvs_wr_u32_le(&slot[6], seq);
    bm_nvs_wr_u16_le(&slot[10], payload_len);
    (void)memcpy(&slot[BM_NVS_SLOT_HDR_SIZE], payload, payload_len);
    crc = bm_crc32(slot, BM_NVS_SLOT_HDR_SIZE + (uint32_t)payload_len);
    bm_nvs_wr_u32_le(&slot[BM_NVS_SLOT_HDR_SIZE + payload_len], crc);

    /* 尾部未用区域保持不变（Flash 后端应先擦除为 0xFF） */
    return BM_OK;
}

int bm_nvs_dual_pick_active(const uint8_t *slot_a, const uint8_t *slot_b,
                            uint32_t slot_size, uint16_t expect_len,
                            int *out_which, uint32_t *out_seq) {
    uint32_t seq_a = 0u;
    uint32_t seq_b = 0u;
    int ok_a;
    int ok_b;

    if (slot_a == NULL || slot_b == NULL || out_which == NULL) {
        return BM_ERR_INVALID;
    }

    ok_a = (bm_nvs_slot_parse(slot_a, slot_size, expect_len, &seq_a, NULL)
            == BM_OK) ? 1 : 0;
    ok_b = (bm_nvs_slot_parse(slot_b, slot_size, expect_len, &seq_b, NULL)
            == BM_OK) ? 1 : 0;

    if (ok_a == 0 && ok_b == 0) {
        return BM_ERR_NOT_FOUND;
    }
    if (ok_a != 0 && ok_b == 0) {
        *out_which = 0;
        if (out_seq != NULL) {
            *out_seq = seq_a;
        }
        return BM_OK;
    }
    if (ok_a == 0 && ok_b != 0) {
        *out_which = 1;
        if (out_seq != NULL) {
            *out_seq = seq_b;
        }
        return BM_OK;
    }
    /* 皆合法：取较大 seq；相等时偏好 B（后写者） */
    if (seq_b >= seq_a) {
        *out_which = 1;
        if (out_seq != NULL) {
            *out_seq = seq_b;
        }
    } else {
        *out_which = 0;
        if (out_seq != NULL) {
            *out_seq = seq_a;
        }
    }
    return BM_OK;
}
