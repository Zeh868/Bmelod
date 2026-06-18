/**
 * @file module_table.c
 * @brief power_buck_loop 示例模块注册表
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 *
 */
#include "bm_module.h"

BM_MODULE_DECLARE(supervisor);

BM_MODULE_TABLE(
    BM_MODULE_ENTRY(supervisor)
);
