/**
 * @file bm_crc32.h
 * @brief 共享 CRC32（Ethernet 多项式 0xEDB88320）工具
 *
 * 单文件 static inline，零堆依赖；供 IPC、分区表等模块复用。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 *
 */
#ifndef BM_CRC32_H
#define BM_CRC32_H

#include <stdint.h>

/**
 * @brief 计算字节序列的 CRC32（Ethernet 多项式，reflected）
 *
 * @param data 输入字节数组
 * @param len  字节长度
 * @return CRC32 校验值
 */
static inline uint32_t bm_crc32(const uint8_t *data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;

    for (i = 0u; i < len; i++) {
        crc ^= data[i];
        uint32_t b;

        for (b = 0u; b < 8u; b++) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

#endif /* BM_CRC32_H */
