/**
 * @file bm_algo_errors.h
 * @brief algorithm 层命名错误码（零 ABI：数值与既有裸返回值完全一致）
 *
 * 审查发现 Source/algorithm 与 include/bm/algorithm 下函数普遍返回裸
 * 数值 -1 表示失败，含义随函数而异（可读性差、调用方难以区分失败原因）。
 * 本头逐点扫描全部 61 处 `return -1;` 后按实际语义归纳为三类命名宏，
 * 数值一律保持 -1 不变（不改变任何 ABI/调用约定），仅将裸数值替换为
 * 语义化名称：
 *
 *   - BM_ALGO_ERR_INVALID：入参非法（NULL/越界/非有限值/配置矛盾等），
 *     覆盖绝大多数调用点（约 55/61）。
 *   - BM_ALGO_ERR_OVERFLOW：某个计数/容量值超出可表达或预分配上限
 *     （如标签计数器绕回、输出缓冲容量不足、静态帧缓冲上限、宽*高
 *     相乘溢出）。
 *   - BM_ALGO_ERR_NOT_FOUND：计算/搜索本身正常完成，但未产生可用结果
 *     （如前景像素计数为 0、区块匹配搜索窗口完全越界导致无候选点）。
 *
 * 少数「NULL 检查」与「容量/范围检查」合并在同一个复合条件分支里
 * （如 `a == NULL || b > MAX`）的调用点，因拆分会改变控制流（超出
 * 本轮"纯改名，不改逻辑"范围），保留为 BM_ALGO_ERR_INVALID。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-07-02
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-02       0.1            zeh            首次归纳并提取（原 Source/algorithm 61 处裸 -1 命名化）
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_ALGO_ERRORS_H
#define BM_ALGO_ERRORS_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 入参非法（NULL / 越界 / 非有限值 / 配置矛盾等），数值同既有裸 -1 */
#define BM_ALGO_ERR_INVALID   (-1)

/** @brief 计数/容量超出可表达或预分配上限，数值同既有裸 -1 */
#define BM_ALGO_ERR_OVERFLOW  (-1)

/** @brief 计算/搜索正常完成但未产生可用结果，数值同既有裸 -1 */
#define BM_ALGO_ERR_NOT_FOUND (-1)

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_ERRORS_H */
