/**
 * @file bm_pipeline.h
 * @brief 编译期静态线性处理链（与 bm_stream 块槽配合）
 *
 * E1 范围：单输入单输出线性链、就地或首节点跨块处理、bypass/reset、
 * 格式链校验。DAG 与 fan-out 留待后续里程碑。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_PIPELINE_H
#define BM_PIPELINE_H

#include "bm/hybrid/bm_block.h"
#include "bm/common/bm_types.h"

#include <stdint.h>

/** 管线节点算子：prepare 在 init 时调用，process 在块消费路径调用 */
typedef struct {
    int (*prepare)(void *state, const void *config);
    int (*process)(void *state,
                   const bm_block_t *input,
                   bm_block_t *output);
    void (*reset)(void *state);
    const char *name;
} bm_pipeline_node_ops_t;

/** 编译期静态节点描述（state/config 由应用分配） */
typedef struct {
    const bm_pipeline_node_ops_t *ops;
    void                         *state;
    const void                   *config;
    uint16_t                      input_format;
    uint16_t                      output_format;
    uint8_t                       bypass;
} bm_pipeline_node_t;

typedef struct {
    bm_pipeline_node_t *nodes;
    uint32_t            node_count;
    int                 initialized;
} bm_pipeline_t;

/**
 * @brief 初始化线性链并调用各节点 prepare
 * @return BM_OK 或 BM_ERR_INVALID（空指针/格式链不匹配/prepare 失败）
 */
int bm_pipeline_init(bm_pipeline_t *pipeline,
                     bm_pipeline_node_t *nodes,
                     uint32_t node_count);

/** @brief 复位链上各节点（跳过 bypass 节点） */
void bm_pipeline_reset(bm_pipeline_t *pipeline);

/**
 * @brief 设置节点 bypass（index 从 0 起）
 * @note 仅允许 input_format == output_format（或其一为 0）的节点 bypass
 * @return BM_OK 或 BM_ERR_INVALID
 */
int bm_pipeline_set_bypass(bm_pipeline_t *pipeline,
                           uint32_t index,
                           int bypass);

/**
 * @brief 就地处理：input 与 output 为同一块时走此路径
 */
int bm_pipeline_process_inplace(bm_pipeline_t *pipeline, bm_block_t *block);

/**
 * @brief 线性处理：首节点读 input，各节点写 output；input==output 时等同就地
 * @return BM_OK，或节点错误；output 容量小于 input 有效长度时返回 BM_ERR_OVERFLOW
 */
int bm_pipeline_process(bm_pipeline_t *pipeline,
                        bm_block_t *input,
                        bm_block_t *output);

#endif /* BM_PIPELINE_H */
