/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_console_stdio.c
 * @brief Console STDIO 后端（stdout/stdin）
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-02
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            初版
 * 2026-07-02       1.1            zeh            QD-7：read 改为真正非阻塞（先探测
 *                                                就绪再读），修复非交互 stdin 无数据
 *                                                时阻塞调用者主循环的问题
 *
 */
#include "bm_types.h"

#include <stdio.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#include <conio.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/select.h>
#include <unistd.h>
#endif

/**
 * @brief 初始化 STDIO 后端
 */
int bm_console_stdio_init(void) {
    return BM_OK;
}

/**
 * @brief 经 stdout 写出
 */
int bm_console_stdio_write(const uint8_t *data, size_t len) {
    if (!data || len == 0u) {
        return BM_ERR_INVALID;
    }
    if (fwrite(data, 1, len, stdout) != len) {
        return BM_ERR_BUSY;
    }
    fflush(stdout);
    return BM_OK;
}

#if defined(_WIN32)
/**
 * @brief Windows 平台非阻塞 stdin 读
 *
 * 交互终端（tty）用 `_kbhit()` 探测按键就绪后再 `_getch()` 取出，避免等待键盘
 * 输入阻塞主循环；非终端（管道/重定向文件/NUL 设备）先用 `PeekNamedPipe`
 * 探测管道是否有就绪字节（无数据立即返回 0），磁盘文件/NUL 设备本身读取不阻塞，
 * 直接读取即可。任何异常路径均 fail-closed 返回已读字节数（可能为 0）。
 *
 * @param data 输出缓冲
 * @param max_len 缓冲容量
 * @return 实际读取字节数（可能为 0，从不阻塞）
 */
static size_t console_stdio_read_platform(uint8_t *data, size_t max_len) {
    size_t n = 0u;
    int fd = _fileno(stdin);

    if (fd < 0) {
        return 0u;
    }

    if (_isatty(fd)) {
        while (n < max_len && _kbhit()) {
            int c = _getch();

            if (c == EOF) {
                break;
            }
            data[n++] = (uint8_t)c;
        }
        return n;
    }

    {
        HANDLE h = (HANDLE)_get_osfhandle(fd);

        if (h == INVALID_HANDLE_VALUE) {
            return 0u;
        }
        if (GetFileType(h) == FILE_TYPE_PIPE) {
            DWORD avail = 0u;

            if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL) ||
                avail == 0u) {
                /* 写端未就绪/已关闭/探测失败：无数据，立即返回 */
                return 0u;
            }
        }
    }

    /* 磁盘文件 / NUL 设备读取本身不阻塞，直接读取即可 */
    while (n < max_len) {
        int c = getchar();

        if (c == EOF) {
            break;
        }
        data[n++] = (uint8_t)c;
    }
    return n;
}
#elif defined(__unix__) || defined(__APPLE__)
/**
 * @brief POSIX 平台非阻塞 stdin 读
 *
 * 每字节先用零超时 `select()` 探测 fd 0 是否就绪，就绪才 `read()`，
 * 否则立即返回已读字节数（可能为 0），从不阻塞调用者。
 *
 * @param data 输出缓冲
 * @param max_len 缓冲容量
 * @return 实际读取字节数（可能为 0，从不阻塞）
 */
static size_t console_stdio_read_platform(uint8_t *data, size_t max_len) {
    size_t n = 0u;

    while (n < max_len) {
        fd_set rfds;
        struct timeval tv;
        int rc;

        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        rc = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
        if (rc <= 0 || !FD_ISSET(STDIN_FILENO, &rfds)) {
            break; /* 无数据就绪 */
        }
        {
            ssize_t r = read(STDIN_FILENO, &data[n], 1u);

            if (r <= 0) {
                break; /* EOF 或读错误 */
            }
            n++;
        }
    }
    return n;
}
#else
/**
 * @brief 未知平台兜底实现（阻塞读）
 *
 * 本项目 native 目标仅覆盖 Windows 与 POSIX；移植到其他宿主平台时需要
 * 补充对应的非阻塞探测实现，此兜底路径保留旧行为以确保可编译。
 *
 * @param data 输出缓冲
 * @param max_len 缓冲容量
 * @return 实际读取字节数
 */
static size_t console_stdio_read_platform(uint8_t *data, size_t max_len) {
    size_t n = 0u;

    while (n < max_len) {
        int c = getchar();

        if (c == EOF) {
            break;
        }
        data[n++] = (uint8_t)c;
    }
    return n;
}
#endif

/**
 * @brief 经 stdin 非阻塞读（无数据时立即返回 0，从不阻塞调用者）
 */
size_t bm_console_stdio_read(uint8_t *data, size_t max_len) {
    if (!data || max_len == 0u) {
        return 0u;
    }
    return console_stdio_read_platform(data, max_len);
}
