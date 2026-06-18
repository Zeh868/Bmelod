/**
 * @file interrupt_timer.h
 * @brief TIMER1 比较中断定时器驱动接口
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
#ifndef INTERRUPT_TIMER_H
#define INTERRUPT_TIMER_H

/** 定时器中断回调函数类型 */
typedef void (*interrupt_timer_callback_t)(void);

/** 初始化 TIMER1 并注册比较中断回调 */
void interrupt_timer_init(interrupt_timer_callback_t callback);

#endif /* INTERRUPT_TIMER_H */
