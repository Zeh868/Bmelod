/**
 * @file module_table.c
 * @brief bus_servo 示例模块注册表
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-25
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-25       1.0            zeh            初稿
 *
 */
#include "bm_module.h"

BM_MODULE_DECLARE(supervisor);

BM_MODULE_TABLE(
    BM_MODULE_ENTRY(supervisor)
);
