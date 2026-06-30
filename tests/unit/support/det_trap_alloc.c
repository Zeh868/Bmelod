/**
 * @file det_trap_alloc.c
 * @brief 零分配权威证据——abort-on-alloc 陷阱分配器（GNU --wrap）
 *
 * 链接期用 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free 重定向。
 * 任一动态分配被调用即打印诊断并 abort：全套用例跑到结束 = 零分配铁证（spec §4.2）。
 * 权威话术："全套用例在 abort-on-alloc 分配器下跑到结束"——不可辩驳，强于计数==0。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-30
 */
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

/** @brief malloc 陷阱：调用即报错并 abort */
void *__wrap_malloc(size_t size)
{
    (void)size;
    fprintf(stderr, "[DET-TRAP] malloc() called — framework MUST be zero-alloc\n");
    fflush(stderr);
    abort();
}

/** @brief calloc 陷阱 */
void *__wrap_calloc(size_t nmemb, size_t size)
{
    (void)nmemb; (void)size;
    fprintf(stderr, "[DET-TRAP] calloc() called\n");
    fflush(stderr);
    abort();
}

/** @brief realloc 陷阱 */
void *__wrap_realloc(void *ptr, size_t size)
{
    (void)ptr; (void)size;
    fprintf(stderr, "[DET-TRAP] realloc() called\n");
    fflush(stderr);
    abort();
}

/** @brief free 陷阱：零分配框架亦不应调用 free */
void __wrap_free(void *ptr)
{
    (void)ptr;
    fprintf(stderr, "[DET-TRAP] free() called\n");
    fflush(stderr);
    abort();
}
