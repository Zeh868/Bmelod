/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_input_debounce.h
 * @brief 通用输入消抖组件
 *
 * 对 GPIO/EXTI 输入做时间滤波：原始电平须持续稳定超过 `stable_us` 才视为
 * 有效沿，输出滤波后电平并上报稳定沿事件。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增通用输入消抖组件
 * 2026-07-28       1.1            zeh            防抖词汇与算法下沉 bm/common
 */
#ifndef BM_INPUT_DEBOUNCE_H
#define BM_INPUT_DEBOUNCE_H

#include "bm/common/bm_input_debounce.h"

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 校验配置
 * @return BM_OK 合法；BM_ERR_INVALID 非法
 */
int bm_input_debounce_validate_config(const bm_input_debounce_config_t *config);

/**
 * @brief 初始化消抖实例
 * @param deb 实例指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法
 */
int bm_input_debounce_init(bm_input_debounce_t *deb);

/**
 * @brief 复位消抖状态
 * @param deb 实例指针；NULL 静默返回
 */
void bm_input_debounce_reset(bm_input_debounce_t *deb);

/**
 * @brief 喂入新的原始采样
 *
 * 当原始电平在 stable_us 内未再翻转时，更新 filtered 并返回 1 表示
 * 发生一次稳定沿事件；否则返回 0。
 *
 * @param deb   实例指针
 * @param raw   当前原始电平（0/1）
 * @param now_us 当前单调时间戳（µs）
 * @return 1 发生稳定沿事件；0 未发生；实例为 NULL 时返回 0
 */
int bm_input_debounce_update(bm_input_debounce_t *deb, int raw, uint64_t now_us);

/**
 * @brief 读取滤波后稳定电平
 * @param deb 实例指针
 * @return 0/1；NULL 时返回 0
 */
int bm_input_debounce_filtered(const bm_input_debounce_t *deb);

/**
 * @brief 查询是否已稳定
 * @param deb 实例指针
 * @return true 已稳定；NULL 时返回 false
 */
bool bm_input_debounce_is_stable(const bm_input_debounce_t *deb);

#ifdef __cplusplus
}
#endif

#endif /* BM_INPUT_DEBOUNCE_H */
