/**
 * @file bm_hal_console.h
 * @brief 诊断字符流 Console HAL（日志 / Shell 分通道、编译期选后端）
 *
 * LOG 通道供 `bm_log_output` drain 使用；CLI 通道供 `bm_shell` 交互。
 * 多核下日志仅 bootstrap 核 drain 后写出；CLI 读写默认仅 bootstrap 核。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            初版：双通道 Console HAL
 *
 */
#ifndef BM_HAL_CONSOLE_H
#define BM_HAL_CONSOLE_H

#include <stddef.h>
#include <stdint.h>

/** Console 逻辑通道 */
typedef enum {
    BM_CONSOLE_LOG = 0, /**< 诊断日志（单向为主） */
    BM_CONSOLE_CLI = 1, /**< Shell 交互（双向） */
} bm_console_ch_t;

#ifndef BM_CONSOLE_BACKEND_NONE
/** 后端：无输出 */
#define BM_CONSOLE_BACKEND_NONE    0
#endif
#ifndef BM_CONSOLE_BACKEND_STDIO
/** 后端：标准 I/O（native_sim / PC） */
#define BM_CONSOLE_BACKEND_STDIO   1
#endif
#ifndef BM_CONSOLE_BACKEND_UART
/** 后端：UART HAL */
#define BM_CONSOLE_BACKEND_UART    2
#endif
#ifndef BM_CONSOLE_BACKEND_RTT
/** 后端：SEGGER RTT 或内置仿真缓冲 */
#define BM_CONSOLE_BACKEND_RTT     3
#endif

#ifndef BM_CONFIG_CONSOLE_LOG_BACKEND
#define BM_CONFIG_CONSOLE_LOG_BACKEND  BM_CONSOLE_BACKEND_NONE
#endif

#ifndef BM_CONFIG_CONSOLE_CLI_BACKEND
#define BM_CONFIG_CONSOLE_CLI_BACKEND  BM_CONSOLE_BACKEND_NONE
#endif

#ifndef BM_CONFIG_CONSOLE_MP_CLI_BOOTSTRAP_ONLY
/** 多核时 CLI 仅 bootstrap 核可读写 */
#define BM_CONFIG_CONSOLE_MP_CLI_BOOTSTRAP_ONLY  1
#endif

/**
 * @brief 初始化 Console 后端（按通道配置去重初始化）
 *
 * @return BM_OK 成功；否则为平台错误码
 */
int bm_hal_console_init(void);

/**
 * @brief 向指定通道写入数据
 *
 * hard RT 剖面下 CLI 通道返回 BM_ERR_NOT_SUPPORTED。
 * 多核且 BM_CONFIG_CONSOLE_MP_CLI_BOOTSTRAP_ONLY 时，非 bootstrap 核写 CLI 被拒绝。
 *
 * @param ch 逻辑通道
 * @param data 数据缓冲区（len>0 时不可为 NULL）
 * @param len 字节数
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_NOT_SUPPORTED 策略拒绝
 */
int bm_hal_console_write(bm_console_ch_t ch, const uint8_t *data, size_t len);

/**
 * @brief 从指定通道非阻塞读取数据
 *
 * @param ch 逻辑通道（通常仅 CLI）
 * @param data 接收缓冲区
 * @param max_len 缓冲区容量
 * @return 实际读取字节数；策略拒绝或后端未就绪时返回 0
 */
size_t bm_hal_console_read(bm_console_ch_t ch, uint8_t *data, size_t max_len);

#endif /* BM_HAL_CONSOLE_H */
