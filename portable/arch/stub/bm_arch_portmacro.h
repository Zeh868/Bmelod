/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_portmacro.h
 * @brief 桩架构宏（CI 无硬件单元测试）
 *
 * 临界区烟雾与 arch_stub 后端链接时使用；屏障与让步均为空操作。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            正式发布
 *
 */

#ifndef BM_ARCH_PORTMACRO_H
#define BM_ARCH_PORTMACRO_H

#define BM_ARCH_DMB() ((void)0)
#define BM_ARCH_YIELD() ((void)0)
#define BM_ARCH_ALIGN(n)

#endif /* BM_ARCH_PORTMACRO_H */
