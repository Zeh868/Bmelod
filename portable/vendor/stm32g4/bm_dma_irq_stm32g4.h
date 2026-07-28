/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_dma_irq_stm32g4.h
 * @brief STM32G4 DMA 通道中断统一路由器
 *
 * 每个 DMA{1,2}_Channel{1..8} 向量仅在本模块定义一个 Handler；USART/SPI 等
 * 驱动通过 register/unregister 挂接回调，避免多后端多重定义或运行时改通道后
 * 中断无人处理。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 DMA IRQ 统一路由器
 *
 */
#ifndef BM_DMA_IRQ_STM32G4_H
#define BM_DMA_IRQ_STM32G4_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief DMA 通道中断回调；ISR 上下文，须有界非阻塞。 */
typedef void (*bm_dma_irq_handler_t)(void *ctx);

/**
 * @brief 注册 DMA 通道中断回调
 *
 * 同一 (ctrl, ch) 已由相同 handler+ctx 占用时幂等返回 BM_OK；
 * 已被不同占用者占用时返回 BM_ERR_BUSY（拒绝覆盖）。
 *
 * @param ctrl    DMA 控制器编号（1=DMA1，2=DMA2）
 * @param ch      通道号（1-based，1..8）
 * @param handler 回调（非 NULL）
 * @param ctx     透传给回调的上下文（可为 NULL）
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法；BM_ERR_BUSY 通道已被占用
 */
int bm_dma_irq_register(uint8_t ctrl, uint8_t ch,
                        bm_dma_irq_handler_t handler, void *ctx);

/**
 * @brief 注销 DMA 通道中断回调
 *
 * 未注册时静默成功。注销后该向量若再触发，路由器清除通道标志后返回。
 *
 * @param ctrl DMA 控制器编号（1=DMA1，2=DMA2）
 * @param ch   通道号（1-based，1..8）
 */
void bm_dma_irq_unregister(uint8_t ctrl, uint8_t ch);

#ifdef __cplusplus
}
#endif

#endif /* BM_DMA_IRQ_STM32G4_H */
