/**
 * @file hybrid_print.c
 * @brief 混合域示例统一输出实现
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
#include "hybrid_print.h"

#include "bm_log.h"
#include "example_support.h"

#include <string.h>

#define TAG "example"

void hybrid_print(const char *text) {
    BM_LOGD(TAG, "hybrid_print: %s", text ? text : "(null)");
    example_print(text);
}

void hybrid_print_pass(const char *example_name) {
    BM_LOGI(TAG, "PASS: %s", example_name ? example_name : "(null)");
    example_print("EXAMPLE_");
    example_print(example_name);
    example_print(": PASS\n");
}
