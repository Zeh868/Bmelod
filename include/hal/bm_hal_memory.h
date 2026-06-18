/**
 * @file bm_hal_memory.h
 * @brief 内存屏障 HAL 接口
 *
 * 提供 release 与 full 屏障，供快照等无锁结构保证可见性。
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
#ifndef BM_HAL_MEMORY_H
#define BM_HAL_MEMORY_H

/**
 * @brief 写屏障，确保此前写操作对读者可见
 */
void bm_memory_barrier_release(void);

/**
 * @brief 全屏障，保证读写操作的有序性
 */
void bm_memory_barrier_full(void);

#endif /* BM_HAL_MEMORY_H */
