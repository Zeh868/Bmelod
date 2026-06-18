/**
 * @file bm_log_output.c
 * @brief 默认日志输出提供者
 *
 * 独立编译单元，便于在不支持弱符号的工具链上由应用提供强符号 `bm_log_output`。
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-06-15       1.1            zeh            hard RT 禁止 stdio 输出
 *
 */
#include "bm_log.h"

#if BM_CONFIG_HARD_RT_PROFILE && BM_CONFIG_LOG_USE_STDIO
#error "hard RT profile forbids stdio log output"
#endif

#if BM_CONFIG_LOG_USE_STDIO
#include <stdio.h>
#endif

/**
 * @brief 默认日志输出钩子
 *
 * 当 BM_CONFIG_LOG_USE_STDIO 启用时写入 stdout 并刷新；否则为空操作。
 * 应用可链接强符号覆盖此弱实现以重定向到 UART/文件等。
 *
 * @param buf 日志缓冲区指针
 * @param len 日志长度
 */
void bm_log_output(const char *buf, size_t len) {
#if BM_CONFIG_LOG_USE_STDIO
    fwrite(buf, 1, len, stdout);
    fflush(stdout);
#else
    (void)buf;
    (void)len;
#endif
}
