/**
 * @file bm_pipeline.c
 * @brief 静态线性 bm_pipeline 实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-15       1.1            zeh            init 后禁止运行期 bypass 变更
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/hybrid/bm_pipeline.h"

#include <string.h>

/** @brief 判断节点是否允许 bypass（输入输出格式一致或均为 0） */
static int bypass_allowed(const bm_pipeline_node_t *node) {
    if (node->input_format != 0u && node->output_format != 0u &&
        node->input_format != node->output_format) {
        return 0;
    }
    return 1;
}

/**
 * @brief 拷贝块元数据（不拷贝 payload）
 *
 * @param src 源块指针
 * @param dst 目标块指针
 * @return BM_OK 成功；负值表示失败
 */
static int copy_block_metadata(const bm_block_t *src, bm_block_t *dst) {
    if (src == NULL || dst == NULL) {
        return BM_ERR_INVALID;
    }
    if (src == dst) {
        return BM_OK;
    }
    if (src->valid_bytes > dst->capacity_bytes) {
        return BM_ERR_OVERFLOW;
    }

    dst->valid_bytes = src->valid_bytes;
    dst->sequence = src->sequence;
    dst->timestamp = src->timestamp;
    dst->format = src->format;
    dst->flags = src->flags;
    return BM_OK;
}

/**
 * @brief 拷贝块元数据与 payload
 *
 * @param src 源块指针
 * @param dst 目标块指针
 * @return BM_OK 成功；负值表示失败
 */
static int copy_block_payload(const bm_block_t *src, bm_block_t *dst) {
    int rc;

    rc = copy_block_metadata(src, dst);
    if (rc != BM_OK || src == dst || src->valid_bytes == 0u) {
        return rc;
    }
    if (src->data == NULL || dst->data == NULL) {
        return BM_ERR_INVALID;
    }

    (void)memmove(dst->data, src->data, src->valid_bytes);
    return BM_OK;
}

/**
 * @brief 校验节点链的格式兼容性与 bypass 合法性
 *
 * @param nodes 节点数组
 * @param node_count 节点数量
 * @return BM_OK 通过；BM_ERR_INVALID 不兼容
 */
static int validate_format_chain(const bm_pipeline_node_t *nodes,
                                 uint32_t node_count) {
    uint32_t i;
    uint16_t prev_output = 0u;

    for (i = 0u; i < node_count; ++i) {
        const bm_pipeline_node_t *node = &nodes[i];

        if (node->ops == NULL) {
            return BM_ERR_INVALID;
        }
        if ((node->bypass != 0u || node->ops->process == NULL) &&
            !bypass_allowed(node)) {
            return BM_ERR_INVALID;
        }
        if (i > 0u && prev_output != 0u && node->input_format != 0u &&
            node->input_format != prev_output) {
            return BM_ERR_INVALID;
        }
        prev_output = node->output_format;
    }
    return BM_OK;
}

int bm_pipeline_init(bm_pipeline_t *pipeline,
                     bm_pipeline_node_t *nodes,
                     uint32_t node_count) {
    uint32_t i;
    int rc;

    if (pipeline == NULL || nodes == NULL || node_count == 0u) {
        return BM_ERR_INVALID;
    }

    rc = validate_format_chain(nodes, node_count);
    if (rc != BM_OK) {
        return rc;
    }

    pipeline->nodes = nodes;
    pipeline->node_count = node_count;
    pipeline->initialized = 0;

    for (i = 0u; i < node_count; ++i) {
        bm_pipeline_node_t *node = &nodes[i];

        if (node->ops->prepare != NULL) {
            rc = node->ops->prepare(node->state, node->config);
            if (rc != BM_OK) {
                /*
                 * 确定性回滚：prepare 失败时逆序 reset 已 prepare 的节点，
                 * 确保失败后管线处于一致状态，可安全重试 init。
                 */
                while (i > 0u) {
                    --i;
                    if (nodes[i].ops->reset != NULL) {
                        nodes[i].ops->reset(nodes[i].state);
                    }
                }
                return rc;
            }
        }
    }

    pipeline->initialized = 1;
    return BM_OK;
}

void bm_pipeline_reset(bm_pipeline_t *pipeline) {
    uint32_t i;

    if (pipeline == NULL || pipeline->initialized == 0) {
        return;
    }

    for (i = 0u; i < pipeline->node_count; ++i) {
        bm_pipeline_node_t *node = &pipeline->nodes[i];

        if (node->bypass != 0u || node->ops == NULL ||
            node->ops->reset == NULL) {
            continue;
        }
        node->ops->reset(node->state);
    }
}

int bm_pipeline_set_bypass(bm_pipeline_t *pipeline,
                           uint32_t index,
                           int bypass) {
    if (pipeline == NULL || pipeline->nodes == NULL ||
        index >= pipeline->node_count) {
        return BM_ERR_INVALID;
    }
    if (pipeline->initialized != 0) {
        return BM_ERR_BUSY;
    }

    if (bypass != 0 && !bypass_allowed(&pipeline->nodes[index])) {
        return BM_ERR_INVALID;
    }

    pipeline->nodes[index].bypass = bypass != 0 ? 1u : 0u;
    return BM_OK;
}

/**
 * @brief 执行单个管线节点
 *
 * @param node 节点指针
 * @param input 输入块指针
 * @param output 输出块指针
 * @return BM_OK 成功；负值表示失败
 */
static int run_node(const bm_pipeline_node_t *node,
                    const bm_block_t *input,
                    bm_block_t *output) {
    int rc;

    if (node->ops == NULL) {
        return BM_ERR_INVALID;
    }
    if (node->bypass != 0u || node->ops->process == NULL) {
        if (!bypass_allowed(node)) {
            return BM_ERR_INVALID;
        }
        return copy_block_payload(input, output);
    }

    if (input->format != 0u && node->input_format != 0u &&
        input->format != node->input_format) {
        return BM_ERR_INVALID;
    }

    rc = copy_block_metadata(input, output);
    if (rc != BM_OK) {
        return rc;
    }

    rc = node->ops->process(node->state, input, output);
    if (rc != BM_OK) {
        return rc;
    }

    if (node->output_format != 0u) {
        output->format = node->output_format;
    }
    return BM_OK;
}

int bm_pipeline_process_inplace(bm_pipeline_t *pipeline, bm_block_t *block) {
    uint32_t i;
    int rc;

    if (pipeline == NULL || block == NULL || pipeline->initialized == 0) {
        return BM_ERR_INVALID;
    }

    for (i = 0u; i < pipeline->node_count; ++i) {
        rc = run_node(&pipeline->nodes[i], block, block);
        if (rc != BM_OK) {
            return rc;
        }
    }
    return BM_OK;
}

int bm_pipeline_process(bm_pipeline_t *pipeline,
                        bm_block_t *input,
                        bm_block_t *output) {
    uint32_t i;
    int rc;

    if (pipeline == NULL || input == NULL || output == NULL ||
        pipeline->initialized == 0) {
        return BM_ERR_INVALID;
    }

    if (input == output) {
        return bm_pipeline_process_inplace(pipeline, input);
    }

    for (i = 0u; i < pipeline->node_count; ++i) {
        const bm_block_t *in_block = (i == 0u) ? input : output;

        rc = run_node(&pipeline->nodes[i], in_block, output);
        if (rc != BM_OK) {
            return rc;
        }
    }
    return BM_OK;
}
