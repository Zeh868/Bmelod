/**
 * @file det_trap_alloc.c
 * @brief 零分配权威证据——可武装的 abort-on-alloc 陷阱分配器（GNU --wrap）
 *
 * 链接期用 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free 重定向。
 * 陷阱带 armed 标志：未武装时转发 __real_*（真实分配，放行 unity/printf 等
 * 测试自身 I/O）；仅在被测窗口 det_trap_arm()..det_trap_disarm() 之间，
 * 任一动态分配即打印诊断并 abort。窗口内跑到结束 = 框架零分配铁证（spec §4.2），
 * 与 MSVC _CrtSetAllocHook 的装卸窗口语义对称。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-30
 */
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

/* --wrap 为每个被 wrap 的符号提供的真实实现 */
extern void *__real_malloc(size_t size);
extern void *__real_calloc(size_t nmemb, size_t size);
extern void *__real_realloc(void *ptr, size_t size);
extern void  __real_free(void *ptr);

/** 陷阱武装标志：0=放行转发真实分配，1=任一分配即 abort */
static int g_det_trap_armed = 0;

/** @brief 武装陷阱：进入被测窗口前调用，之后任一动态分配即 abort */
void det_trap_arm(void)    { g_det_trap_armed = 1; }

/** @brief 解除武装：离开被测窗口后调用，恢复转发真实分配 */
void det_trap_disarm(void) { g_det_trap_armed = 0; }

/** @brief malloc 陷阱：武装时 abort，否则转发真实 malloc */
void *__wrap_malloc(size_t size)
{
    if (g_det_trap_armed) {
        fprintf(stderr, "[DET-TRAP] malloc() called — framework MUST be zero-alloc\n");
        fflush(stderr);
        abort();
    }
    return __real_malloc(size);
}

/** @brief calloc 陷阱：武装时 abort，否则转发真实 calloc */
void *__wrap_calloc(size_t nmemb, size_t size)
{
    if (g_det_trap_armed) {
        fprintf(stderr, "[DET-TRAP] calloc() called\n");
        fflush(stderr);
        abort();
    }
    return __real_calloc(nmemb, size);
}

/** @brief realloc 陷阱：武装时 abort，否则转发真实 realloc */
void *__wrap_realloc(void *ptr, size_t size)
{
    if (g_det_trap_armed) {
        fprintf(stderr, "[DET-TRAP] realloc() called\n");
        fflush(stderr);
        abort();
    }
    return __real_realloc(ptr, size);
}

/** @brief free 陷阱：武装时 abort（零分配框架亦不应 free），否则转发真实 free */
void __wrap_free(void *ptr)
{
    if (g_det_trap_armed) {
        fprintf(stderr, "[DET-TRAP] free() called\n");
        fflush(stderr);
        abort();
    }
    __real_free(ptr);
}
