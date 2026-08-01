/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_nvs_dual_slot.h
 * @brief NVS 双槽格式：魔数/版本/序号/载荷/CRC（平台无关）
 * @maturity E1
 *
 * 槽布局（小端）：
 * @code
 * [magic 4B 'B','M','N','S'] [version 1B][pad 1B] [seq 4B] [payload_len 2B]
 * [payload payload_len B] [crc32 4B]  — CRC 覆盖 magic..payload 末字节
 * @endcode
 *
 * load 选 seq 更大且 CRC/magic/version/长度合法的槽；save 写入 inactive 槽。
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
#ifndef BM_NVS_DUAL_SLOT_H
#define BM_NVS_DUAL_SLOT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 槽魔数字节 */
#define BM_NVS_SLOT_MAGIC0  ((uint8_t)'B')
#define BM_NVS_SLOT_MAGIC1  ((uint8_t)'M')
#define BM_NVS_SLOT_MAGIC2  ((uint8_t)'N')
#define BM_NVS_SLOT_MAGIC3  ((uint8_t)'S')
/** @brief 槽格式版本 */
#define BM_NVS_SLOT_VERSION 1u
/** @brief 槽固定头长度（不含 payload 与 CRC） */
#define BM_NVS_SLOT_HDR_SIZE 12u
/** @brief 槽尾 CRC 长度 */
#define BM_NVS_SLOT_CRC_SIZE 4u
/** @brief 槽开销（头 + CRC） */
#define BM_NVS_SLOT_OVERHEAD (BM_NVS_SLOT_HDR_SIZE + BM_NVS_SLOT_CRC_SIZE)

/**
 * @brief 计算容纳指定载荷所需的最小槽字节数
 *
 * @param payload_len 载荷长度
 * @return 最小槽大小；溢出时返回 0
 */
uint32_t bm_nvs_slot_min_size(uint16_t payload_len);

/**
 * @brief 解析槽：校验 magic/version/CRC/payload_len
 *
 * @param slot         槽缓冲
 * @param slot_size    槽容量
 * @param expect_len   期望载荷长度（与 bm_hal_nvs_load 的 size 一致）
 * @param out_seq      输出序号；可为 NULL
 * @param out_payload  输出指向槽内载荷的指针；可为 NULL
 * @return BM_OK 合法；BM_ERR_INVALID 损坏/不匹配；BM_ERR_NOT_FOUND 空/擦除态
 */
int bm_nvs_slot_parse(const uint8_t *slot, uint32_t slot_size,
                      uint16_t expect_len, uint32_t *out_seq,
                      const uint8_t **out_payload);

/**
 * @brief 将载荷封装进槽缓冲（调用方保证 slot 已擦为 0xFF 或可覆盖写）
 *
 * @param slot       槽缓冲
 * @param slot_size  槽容量
 * @param seq        序号（应严格大于当前 active）
 * @param payload    载荷
 * @param payload_len 载荷长度
 * @return BM_OK；BM_ERR_INVALID 参数非法；BM_ERR_OVERFLOW 槽太小
 */
int bm_nvs_slot_pack(uint8_t *slot, uint32_t slot_size, uint32_t seq,
                     const uint8_t *payload, uint16_t payload_len);

/**
 * @brief 在双槽中选择 active（较大 seq 且合法）
 *
 * @param slot_a / slot_b 两槽缓冲
 * @param slot_size       单槽容量
 * @param expect_len      期望载荷长度
 * @param out_which       输出 0=A、1=B；皆无效时不改写
 * @param out_seq         输出 active 序号；可为 NULL
 * @return BM_OK 找到；BM_ERR_NOT_FOUND 皆无效
 */
int bm_nvs_dual_pick_active(const uint8_t *slot_a, const uint8_t *slot_b,
                            uint32_t slot_size, uint16_t expect_len,
                            int *out_which, uint32_t *out_seq);

#ifdef __cplusplus
}
#endif

#endif /* BM_NVS_DUAL_SLOT_H */
