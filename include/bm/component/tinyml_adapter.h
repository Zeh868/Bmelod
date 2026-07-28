/**
 * @file tinyml_adapter.h
 * @brief TinyML 静态 arena、张量量化与最小算子图执行接口。
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            静态 arena 骨架
 * 2026-06-23       1.1            zeh            补 SPDX 与函数级 Doxygen
 * 2026-07-28       1.2            zeh            共享 arena、图与张量契约下沉至 bm/common
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_TINYML_ADAPTER_H
#define BM_TINYML_ADAPTER_H

#include "bm/common/bm_tinyml_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 复位静态 arena。
 * @param arena arena 实例；为 NULL 时静默返回。
 */
void bm_tinyml_arena_reset(bm_tinyml_arena_t *arena);

/**
 * @brief 查询 arena 已分配字节数。
 * @param arena arena 实例；为 NULL 时返回 0。
 * @return 当前 offset 载荷值（字节）。
 */
uint32_t bm_tinyml_arena_bytes_used(const bm_tinyml_arena_t *arena);

/**
 * @brief 从 arena 按对齐要求分配内存。
 * @param arena arena 实例。
 * @param size 请求字节数。
 * @param align 对齐字节数；0 时使用 4 字节。
 * @return 成功时为已对齐指针；参数无效或空间不足时为 NULL。
 */
void *bm_tinyml_arena_alloc(bm_tinyml_arena_t *arena, uint32_t size, uint32_t align);

/**
 * @brief 分配 int8 张量并填写元数据。
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_OVERFLOW 维度乘积溢出；
 *         BM_ERR_NO_MEM arena 空间不足。
 */
int bm_tinyml_tensor_alloc_i8(bm_tinyml_arena_t *arena,
                              bm_tinyml_tensor_t *tensor,
                              const uint32_t *dims,
                              uint32_t ndim,
                              const bm_tinyml_quant_params_t *quant);

/**
 * @brief 将 float32 缓冲量化写入张量。
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效或元素数超过张量容量。
 */
int bm_tinyml_tensor_quantize_f32(const bm_tinyml_tensor_t *tensor,
                                  const float *src,
                                  uint32_t count);

/**
 * @brief 将张量反量化写入 float32 缓冲。
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效或元素数超过张量容量。
 */
int bm_tinyml_tensor_dequantize_f32(const bm_tinyml_tensor_t *tensor,
                                    float *dst,
                                    uint32_t count);

/**
 * @brief 校验最小算子图的节点与张量索引。
 * @return BM_OK 成功；BM_ERR_INVALID 图、节点或索引无效。
 */
int bm_tinyml_graph_init(bm_tinyml_graph_t *graph);

/**
 * @brief 按节点顺序执行最小算子图。
 * @return BM_OK 成功；BM_ERR_INVALID 图、节点或输入无效；BM_ERR_OVERFLOW
 *         维度计算溢出；BM_ERR_NO_MEM 输出张量或输出缓冲容量不足。
 */
int bm_tinyml_graph_run(bm_tinyml_graph_t *graph,
                        const float *float_inputs,
                        uint32_t float_input_count,
                        float *float_outputs,
                        uint32_t float_output_count);

#ifdef __cplusplus
}
#endif

#endif /* BM_TINYML_ADAPTER_H */
