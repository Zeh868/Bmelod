/**
 * @file example_support.h
 * @brief 示例公共辅助函数声明
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 *
 */
#ifndef EXAMPLE_SUPPORT_H
#define EXAMPLE_SUPPORT_H

#include <stdint.h>

/** 通过 UART 发送以 '\0' 结尾的字符串 */
void example_print(const char *text);

/** 将无符号 32 位整数以十进制形式输出 */
void example_print_u32(uint32_t value);

/** 忙等待指定循环次数（用于简单延时） */
void example_delay_cycles(uint32_t cycles);

#endif /* EXAMPLE_SUPPORT_H */
