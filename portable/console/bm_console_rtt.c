/**
 * @file bm_console_rtt.c
 * @brief Console RTT 后端（SEGGER RTT 或内置仿真上行缓冲）
 *
 * 定义 BM_CONSOLE_HAS_SEGGER_RTT 并链接 SEGGER RTT 库时使用真 RTT；
 * 否则使用有界 RAM 环，便于单测与无 J-Link 环境编译。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            初版
 *
 */
#include "bm_config.h"
#include "bm_types.h"

#include <string.h>

#ifndef BM_CONFIG_CONSOLE_RTT_SIM_BUF_SIZE
#define BM_CONFIG_CONSOLE_RTT_SIM_BUF_SIZE  4096u
#endif

#if defined(BM_CONSOLE_HAS_SEGGER_RTT)
#include "SEGGER_RTT.h"
#endif

#if !defined(BM_CONSOLE_HAS_SEGGER_RTT)
static char     s_rtt_up[BM_CONFIG_CONSOLE_RTT_SIM_BUF_SIZE];
static uint32_t s_rtt_up_len;
static char     s_rtt_down[256];
static uint32_t s_rtt_down_head;
static uint32_t s_rtt_down_tail;
#endif

/**
 * @brief 初始化 RTT 后端
 */
int bm_console_rtt_init(void) {
#if defined(BM_CONSOLE_HAS_SEGGER_RTT)
    SEGGER_RTT_Init();
    return BM_OK;
#else
    s_rtt_up_len = 0u;
    s_rtt_down_head = 0u;
    s_rtt_down_tail = 0u;
    return BM_OK;
#endif
}

/**
 * @brief RTT 上行写
 */
int bm_console_rtt_write(const uint8_t *data, size_t len) {
    if (!data || len == 0u) {
        return BM_ERR_INVALID;
    }
#if defined(BM_CONSOLE_HAS_SEGGER_RTT)
    if (SEGGER_RTT_Write(0, data, len) < 0) {
        return BM_ERR_BUSY;
    }
    return BM_OK;
#else
    uint32_t room;
    uint32_t i;

    room = BM_CONFIG_CONSOLE_RTT_SIM_BUF_SIZE - s_rtt_up_len;
    if (len > room) {
        len = room;
    }
    for (i = 0u; i < len; i++) {
        s_rtt_up[s_rtt_up_len + i] = (char)data[i];
    }
    s_rtt_up_len += (uint32_t)len;
    return BM_OK;
#endif
}

/**
 * @brief RTT 下行读（Shell 输入）
 */
size_t bm_console_rtt_read(uint8_t *data, size_t max_len) {
    size_t n = 0u;

    if (!data || max_len == 0u) {
        return 0u;
    }
#if defined(BM_CONSOLE_HAS_SEGGER_RTT)
    int got;

    got = SEGGER_RTT_Read(0, data, max_len);
    return (got > 0) ? (size_t)got : 0u;
#else
    while (n < max_len && s_rtt_down_head != s_rtt_down_tail) {
        data[n++] = (uint8_t)s_rtt_down[s_rtt_down_tail];
        s_rtt_down_tail = (s_rtt_down_tail + 1u) % (uint32_t)sizeof(s_rtt_down);
    }
    return n;
#endif
}

#if !defined(BM_CONSOLE_HAS_SEGGER_RTT) && defined(BM_ENABLE_CONSOLE_RTT_TEST_HOOK)
/**
 * @brief 单测：读取仿真 RTT 上行缓冲
 */
size_t bm_console_rtt_test_peek_up(char *buf, size_t max_len) {
    size_t n;

    if (!buf || max_len == 0u) {
        return 0u;
    }
    n = s_rtt_up_len;
    if (n >= max_len) {
        n = max_len - 1u;
    }
    memcpy(buf, s_rtt_up, n);
    buf[n] = '\0';
    return n;
}

/**
 * @brief 单测：向仿真 RTT 下行注入字符
 */
void bm_console_rtt_test_feed_down(const uint8_t *data, size_t len) {
    uint32_t i;

    if (!data) {
        return;
    }
    for (i = 0u; i < len; i++) {
        uint32_t next =
            (s_rtt_down_head + 1u) % (uint32_t)sizeof(s_rtt_down);
        if (next == s_rtt_down_tail) {
            break;
        }
        s_rtt_down[s_rtt_down_head] = (char)data[i];
        s_rtt_down_head = next;
    }
}
#endif
