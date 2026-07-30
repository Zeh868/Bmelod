/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file syscalls_stub.c
 * @brief newlib-nano 与 -nostartfiles 兼容桩（`_init` / `_fini`）
 *
 * Cube startup 调用 `__libc_init_array`，nano 会引用 `_init`；本工程不链
 * crt0 的 init 段，提供空实现即可。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-30
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-30       1.0            zeh            新增
 */
void _init(void)
{
}

void _fini(void)
{
}
