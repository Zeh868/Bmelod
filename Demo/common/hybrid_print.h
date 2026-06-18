/**
 * @file hybrid_print.h
 * @brief 混合域示例统一输出接口
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
#ifndef HYBRID_PRINT_H
#define HYBRID_PRINT_H

/** 输出一行文本（无换行后缀时由调用方提供） */
void hybrid_print(const char *text);

/** 输出示例通过标记，格式为 EXAMPLE_<name>: PASS */
void hybrid_print_pass(const char *example_name);

#endif /* HYBRID_PRINT_H */
