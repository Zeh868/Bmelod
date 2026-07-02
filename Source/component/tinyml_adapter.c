/**
 * @file tinyml_adapter.c
 * @brief TinyML 静态 arena、tensor 量化与最小算子图实现
 *
 * bump pointer 分配，tensor 元数据委托 bm_algo_features 量化。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            静态 arena 骨架
 * 2026-06-17       0.2            zeh            quantize/fc/dequantize 图
 * 2026-06-17       0.3            zeh            ReLU 算子
 * 2026-06-17       0.4            zeh            SOFTMAX/FLATTEN 算子
 * 2026-06-17       0.5            zeh            ADD 双输入逐元素饱和相加
 * 2026-06-17       0.6            zeh            MUL 双输入逐元素饱和相乘
 * 2026-06-17       0.7            zeh            MAXPOOL 2x2 stride2 算子
 * 2026-06-17       0.8            zeh            DEPTHWISE 3x3 stride1 算子
 * 2026-06-17       0.9            zeh            CONV2D 1x1 NCHW 算子
 * 2026-06-23       1.0            zeh            通用 CONV2D（任意核/步长/explicit padding）
 * 2026-06-23       1.1            zeh            补 SPDX 与函数级 Doxygen
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/component/tinyml_adapter.h"
#include "bm/algorithm/bm_algo_features.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**
 * @brief 将 value 向上对齐至 align 的整数倍（align 须为 2 的幂或 ≤1）
 */
static uint32_t align_up(uint32_t value, uint32_t align) {
    uint32_t mask;

    if (align <= 1u) {
        return value;
    }
    mask = align - 1u;
    return (value + mask) & ~mask;
}

/**
 * @brief 将 int32 值饱和截断到 int8 范围 [-128, 127]
 */
static int8_t clamp_i8(int32_t value) {
    if (value > 127) {
        return (int8_t)127;
    }
    if (value < -128) {
        return (int8_t)-128;
    }
    return (int8_t)value;
}

/**
 * @brief 复位静态 arena（偏移归零，峰值清零）
 *
 * @param arena arena 实例（NULL 时直接返回）
 */
void bm_tinyml_arena_reset(bm_tinyml_arena_t *arena) {
    if (arena != NULL) {
        arena->offset = 0u;
        arena->peak_bytes = 0u;
    }
}

/**
 * @brief 查询 arena 当前已分配字节数
 *
 * @param arena arena 实例（可为 NULL，返回 0）
 * @return 当前 offset 值（字节）
 */
uint32_t bm_tinyml_arena_bytes_used(const bm_tinyml_arena_t *arena) {
    return (arena != NULL) ? arena->offset : 0u;
}

/**
 * @brief 从 arena 按对齐分配内存（bump pointer，不可释放单块）
 *
 * @param arena arena 实例（不可为 NULL）
 * @param size  需分配字节数（不可为 0）
 * @param align 对齐字节数（0 时默认 4 字节对齐）
 * @return 成功返回对齐后的内存指针；arena 空间不足时返回 NULL
 */
void *bm_tinyml_arena_alloc(bm_tinyml_arena_t *arena,
                            uint32_t size,
                            uint32_t align) {
    uint32_t start;
    uint32_t end;

    if (arena == NULL || size == 0u) {
        return NULL;
    }
    if (align == 0u) {
        align = 4u;
    }

    start = align_up(arena->offset, align);
    end = start + size;
    if (end > BM_TINYML_ARENA_MAX_BYTES) {
        return NULL;
    }

    arena->offset = end;
    if (end > arena->peak_bytes) {
        arena->peak_bytes = end;
    }
    return &arena->storage[start];
}

/**
 * @brief 在 arena 中分配 int8 tensor 并填写元数据
 *
 * @param arena  arena 实例（不可为 NULL）
 * @param tensor 输出 tensor 描述符（不可为 NULL）
 * @param dims   各维度大小数组（不可为 NULL；每个维度须 ≥1）
 * @param ndim   维度数，范围 [1, 4]
 * @param quant  量化参数（可为 NULL，则 scale=1.0 zero_point=0）
 * @return 0 成功；-1 参数无效或 arena 空间不足
 */
int bm_tinyml_tensor_alloc_i8(bm_tinyml_arena_t *arena,
                              bm_tinyml_tensor_t *tensor,
                              const uint32_t *dims,
                              uint32_t ndim,
                              const bm_tinyml_quant_params_t *quant) {
    uint32_t i;
    uint32_t count = 1u;
    int8_t *buf;

    if (arena == NULL || tensor == NULL || dims == NULL ||
        ndim == 0u || ndim > 4u) {
        return -1;
    }

    for (i = 0u; i < ndim; ++i) {
        if (dims[i] == 0u) {
            return -1;
        }
        /* 维度乘积 u32 溢出防护（P2-8）：乘前判 count > UINT32_MAX/dims[i] */
        if (count > UINT32_MAX / dims[i]) {
            return -1;
        }
        count *= dims[i];
    }

    buf = (int8_t *)bm_tinyml_arena_alloc(arena, count, 4u);
    if (buf == NULL) {
        return -1;
    }

    memset(tensor, 0, sizeof(*tensor));
    tensor->data = buf;
    tensor->byte_count = count;
    tensor->ndim = ndim;
    for (i = 0u; i < ndim; ++i) {
        tensor->dims[i] = dims[i];
    }
    if (quant != NULL) {
        tensor->scale = quant->scale;
        tensor->zero_point = quant->zero_point;
    } else {
        tensor->scale = 1.0f;
        tensor->zero_point = 0;
    }
    return 0;
}

/**
 * @brief 将 float32 缓冲量化写入 tensor 的 int8 数据区
 *
 * @param tensor 目标 tensor（不可为 NULL；data 须已分配）
 * @param src    源 float32 缓冲（不可为 NULL）
 * @param count  量化元素数，须 ≤ tensor->byte_count
 * @return 0 成功；-1 参数无效
 */
int bm_tinyml_tensor_quantize_f32(const bm_tinyml_tensor_t *tensor,
                                  const float *src,
                                  uint32_t count) {
    if (tensor == NULL || src == NULL || tensor->data == NULL) {
        return -1;
    }
    if (count > tensor->byte_count) {
        return -1;
    }

    bm_algo_quantize_buffer_f32_i8(src, tensor->data, count,
                                   tensor->scale, tensor->zero_point);
    return 0;
}

/**
 * @brief 将 tensor 的 int8 数据反量化为 float32 并写入 dst
 *
 * @param tensor 源 tensor（不可为 NULL；data 须已分配）
 * @param dst    目标 float32 缓冲（不可为 NULL）
 * @param count  反量化元素数，须 ≤ tensor->byte_count
 * @return 0 成功；-1 参数无效
 */
int bm_tinyml_tensor_dequantize_f32(const bm_tinyml_tensor_t *tensor,
                                    float *dst,
                                    uint32_t count) {
    uint32_t i;

    if (tensor == NULL || dst == NULL || tensor->data == NULL) {
        return -1;
    }
    if (count > tensor->byte_count) {
        return -1;
    }

    for (i = 0u; i < count; ++i) {
        dst[i] = bm_algo_dequantize_i8_to_f32(tensor->data[i],
                                              tensor->scale,
                                              tensor->zero_point);
    }
    return 0;
}

/**
 * @brief 校验图中 tensor 索引是否合法
 */
static int graph_tensor_valid(const bm_tinyml_graph_t *graph, uint32_t index) {
    return (graph != NULL && graph->tensors != NULL &&
            index < graph->tensor_count);
}

/**
 * @brief 全连接算子（FC）：i8 矩阵向量乘，结果 >> 7 后饱和截断
 */
static int run_fc_node(const bm_tinyml_graph_node_t *node,
                       const bm_tinyml_tensor_t *in_tensor,
                       bm_tinyml_tensor_t *out_tensor) {
    uint32_t i;
    uint32_t j;
    uint32_t in_dim;
    uint32_t out_dim;

    if (node == NULL || in_tensor == NULL || out_tensor == NULL ||
        node->fc_weights == NULL || in_tensor->data == NULL ||
        out_tensor->data == NULL) {
        return -1;
    }

    in_dim = node->fc_in_dim;
    out_dim = node->fc_out_dim;
    if (in_dim == 0u || out_dim == 0u ||
        in_tensor->byte_count < in_dim ||
        out_tensor->byte_count < out_dim) {
        return -1;
    }

    for (i = 0u; i < out_dim; ++i) {
        int32_t acc = 0;

        for (j = 0u; j < in_dim; ++j) {
            acc += (int32_t)in_tensor->data[j] *
                   (int32_t)node->fc_weights[i * in_dim + j];
        }
        out_tensor->data[i] = clamp_i8(acc >> 7);
    }
    return 0;
}

/**
 * @brief ReLU：i8 tensor 原地 max(0, x)
 */
static int run_relu_node(bm_tinyml_tensor_t *tensor) {
    uint32_t i;

    if (tensor == NULL || tensor->data == NULL) {
        return -1;
    }

    for (i = 0u; i < tensor->byte_count; ++i) {
        if (tensor->data[i] < 0) {
            tensor->data[i] = 0;
        }
    }
    return 0;
}

#define BM_TINYML_SOFTMAX_MAX  16u

/**
 * @brief int8 softmax（≤16 元素，减 max 防溢出，定点近似）
 */
static int run_softmax_node(bm_tinyml_tensor_t *tensor) {
    uint32_t n;
    uint32_t i;
    int8_t max_v;
    int32_t exp_sum;
    int32_t exp_vals[BM_TINYML_SOFTMAX_MAX];

    if (tensor == NULL || tensor->data == NULL) {
        return -1;
    }
    n = tensor->byte_count;
    if (n == 0u || n > BM_TINYML_SOFTMAX_MAX) {
        return -1;
    }

    max_v = tensor->data[0];
    for (i = 1u; i < n; ++i) {
        if (tensor->data[i] > max_v) {
            max_v = tensor->data[i];
        }
    }

    exp_sum = 0;
    for (i = 0u; i < n; ++i) {
        int32_t shifted = (int32_t)tensor->data[i] - (int32_t)max_v;
        int32_t e;

        if (shifted < -8) {
            e = 0;
        } else if (shifted >= 0) {
            e = 256 << shifted;
            if (e > 32767) {
                e = 32767;
            }
        } else {
            e = 256 >> (-shifted);
        }
        exp_vals[i] = e;
        exp_sum += e;
    }
    if (exp_sum <= 0) {
        return -1;
    }

    for (i = 0u; i < n; ++i) {
        int32_t scaled = (exp_vals[i] * 127) / exp_sum;
        tensor->data[i] = clamp_i8(scaled);
    }
    return 0;
}

/**
 * @brief 展平元数据：ndim→2，复用输入数据指针（无拷贝）
 */
static int run_flatten_node(bm_tinyml_tensor_t *in_tensor,
                            bm_tinyml_tensor_t *out_tensor) {
    uint32_t i;
    uint32_t total = 1u;

    if (in_tensor == NULL || out_tensor == NULL ||
        in_tensor->data == NULL) {
        return -1;
    }
    if (in_tensor->ndim == 0u) {
        return -1;
    }

    for (i = 0u; i < in_tensor->ndim; ++i) {
        total *= in_tensor->dims[i];
    }

    out_tensor->data = in_tensor->data;
    out_tensor->byte_count = in_tensor->byte_count;
    out_tensor->scale = in_tensor->scale;
    out_tensor->zero_point = in_tensor->zero_point;
    out_tensor->ndim = 2u;
    out_tensor->dims[0] = 1u;
    out_tensor->dims[1] = total;
    out_tensor->dims[2] = 0u;
    out_tensor->dims[3] = 0u;
    return 0;
}

/**
 * @brief ADD：input_tensor 与 input_tensor_b 同 shape 逐元素饱和相加（i8）
 */
static int run_add_node(const bm_tinyml_graph_node_t *node,
                        const bm_tinyml_tensor_t *in_a,
                        const bm_tinyml_tensor_t *in_b,
                        bm_tinyml_tensor_t *out_tensor) {
    uint32_t i;

    if (node == NULL || in_a == NULL || in_b == NULL || out_tensor == NULL ||
        in_a->data == NULL || in_b->data == NULL || out_tensor->data == NULL) {
        return -1;
    }
    if (in_a->byte_count != in_b->byte_count ||
        in_a->byte_count != out_tensor->byte_count ||
        in_a->ndim != in_b->ndim) {
        return -1;
    }

    for (i = 0u; i < in_a->byte_count; ++i) {
        out_tensor->data[i] = clamp_i8((int32_t)in_a->data[i] +
                                       (int32_t)in_b->data[i]);
    }
    return 0;
}

/**
 * @brief MUL：input_tensor 与 input_tensor_b 同 shape 逐元素饱和相乘（i8，>>7）
 */
static int run_mul_node(const bm_tinyml_graph_node_t *node,
                        const bm_tinyml_tensor_t *in_a,
                        const bm_tinyml_tensor_t *in_b,
                        bm_tinyml_tensor_t *out_tensor) {
    uint32_t i;

    if (node == NULL || in_a == NULL || in_b == NULL || out_tensor == NULL ||
        in_a->data == NULL || in_b->data == NULL || out_tensor->data == NULL) {
        return -1;
    }
    if (in_a->byte_count != in_b->byte_count ||
        in_a->byte_count != out_tensor->byte_count ||
        in_a->ndim != in_b->ndim) {
        return -1;
    }

    for (i = 0u; i < in_a->byte_count; ++i) {
        int32_t prod = (int32_t)in_a->data[i] * (int32_t)in_b->data[i];
        out_tensor->data[i] = clamp_i8(prod >> 7);
    }
    return 0;
}

/**
 * @brief 2×2 stride-2 最大池化（NCHW-ish 展平：dims[0]=H, dims[1]=W）
 */
static int run_maxpool_2x2_node(const bm_tinyml_tensor_t *in_tensor,
                                bm_tinyml_tensor_t *out_tensor) {
    uint32_t h;
    uint32_t w;
    uint32_t oh;
    uint32_t ow;
    uint32_t y;
    uint32_t x;
    uint32_t dy;
    uint32_t dx;

    if (in_tensor == NULL || out_tensor == NULL ||
        in_tensor->data == NULL || out_tensor->data == NULL) {
        return -1;
    }
    if (in_tensor->ndim != 2u) {
        return -1;
    }

    h = in_tensor->dims[0];
    w = in_tensor->dims[1];
    if (h == 0u || w == 0u || (h & 1u) != 0u || (w & 1u) != 0u) {
        return -1;
    }

    oh = h / 2u;
    ow = w / 2u;

    for (y = 0u; y < oh; ++y) {
        for (x = 0u; x < ow; ++x) {
            int8_t max_v = in_tensor->data[(y * 2u) * w + (x * 2u)];

            for (dy = 0u; dy < 2u; ++dy) {
                for (dx = 0u; dx < 2u; ++dx) {
                    int8_t v = in_tensor->data[((y * 2u) + dy) * w +
                                                ((x * 2u) + dx)];
                    if (v > max_v) {
                        max_v = v;
                    }
                }
            }
            out_tensor->data[y * ow + x] = max_v;
        }
    }

    out_tensor->dims[0] = oh;
    out_tensor->dims[1] = ow;
    out_tensor->byte_count = oh * ow;
    return 0;
}

/**
 * @brief 3×3 depthwise conv，stride=1、same pad=0、单通道 i8（fc_weights 存 9 核）
 */
static int run_depthwise_conv2d_node(const bm_tinyml_graph_node_t *node,
                                     const bm_tinyml_tensor_t *in_tensor,
                                     bm_tinyml_tensor_t *out_tensor) {
    uint32_t k;
    int32_t acc;

    if (node == NULL || in_tensor == NULL || out_tensor == NULL ||
        node->fc_weights == NULL || in_tensor->data == NULL ||
        out_tensor->data == NULL) {
        return -1;
    }
    if (in_tensor->ndim != 2u || in_tensor->dims[0] != 3u ||
        in_tensor->dims[1] != 3u || node->fc_in_dim != 9u) {
        return -1;
    }
    if (out_tensor->byte_count < 1u) {
        return -1;
    }

    acc = 0;
    for (k = 0u; k < 9u; ++k) {
        acc += (int32_t)in_tensor->data[k] * (int32_t)node->fc_weights[k];
    }
    out_tensor->data[0] = clamp_i8(acc >> 7);
    out_tensor->dims[0] = 1u;
    out_tensor->dims[1] = 1u;
    out_tensor->byte_count = 1u;
    return 0;
}

/**
 * @brief 1×1 卷积（NCHW 展平，in_ch→out_ch，权重 layout [out_ch][in_ch]）
 */
static int run_conv2d_1x1_node(const bm_tinyml_graph_node_t *node,
                               const bm_tinyml_tensor_t *in_tensor,
                               bm_tinyml_tensor_t *out_tensor) {
    uint32_t n;
    uint32_t oc;
    uint32_t ic;
    uint32_t h;
    uint32_t w;
    uint32_t n_dim;
    uint32_t c_in_dim;
    uint32_t h_dim;
    uint32_t w_dim;
    uint32_t in_base;
    uint32_t out_base;

    if (node == NULL || in_tensor == NULL || out_tensor == NULL ||
        node->fc_weights == NULL || in_tensor->data == NULL ||
        out_tensor->data == NULL) {
        return -1;
    }
    if (in_tensor->ndim != 4u) {
        return -1;
    }

    n_dim = in_tensor->dims[0];
    c_in_dim = in_tensor->dims[1];
    h_dim = in_tensor->dims[2];
    w_dim = in_tensor->dims[3];
    if (c_in_dim != node->fc_in_dim || c_in_dim == 0u ||
        node->fc_out_dim == 0u ||
        n_dim == 0u || h_dim == 0u || w_dim == 0u) {
        return -1;
    }
    if (in_tensor->byte_count < n_dim * c_in_dim * h_dim * w_dim ||
        out_tensor->byte_count < n_dim * node->fc_out_dim * h_dim * w_dim) {
        return -1;
    }

    for (n = 0u; n < n_dim; ++n) {
        for (h = 0u; h < h_dim; ++h) {
            for (w = 0u; w < w_dim; ++w) {
                for (oc = 0u; oc < node->fc_out_dim; ++oc) {
                    int32_t acc = 0;

                    for (ic = 0u; ic < node->fc_in_dim; ++ic) {
                        in_base = (((n * c_in_dim + ic) * h_dim) + h) * w_dim +
                                    w;
                        acc += (int32_t)in_tensor->data[in_base] *
                               (int32_t)node->fc_weights[oc * node->fc_in_dim +
                                                           ic];
                    }
                    out_base = (((n * node->fc_out_dim + oc) * h_dim) + h) *
                               w_dim + w;
                    out_tensor->data[out_base] = clamp_i8(acc >> 7);
                }
            }
        }
    }

    out_tensor->ndim = 4u;
    out_tensor->dims[0] = n_dim;
    out_tensor->dims[1] = node->fc_out_dim;
    out_tensor->dims[2] = h_dim;
    out_tensor->dims[3] = w_dim;
    out_tensor->byte_count = n_dim * node->fc_out_dim * h_dim * w_dim;
    return 0;
}

/**
 * @brief 通用标准 CONV2D（Cin→Cout，任意 KhxKw，可配 stride 与 explicit padding）
 *
 * 量化语义（沿用现有算子约定）：
 *   acc = bias[oc] + Σ_{ic,kh,kw} in[n][ic][ih][iw] * w[oc][ic][kh][kw]
 *   out[n][oc][oh][ow] = clamp_i8(acc >> 7)
 * 越界像素以 input zero_point（即 i8 值 0，表示数值 0）填充。
 *
 * 输入 tensor：ndim==4，[N][Cin][H][W]，NCHW i8
 * 输出 tensor：预分配缓冲 ≥ N*Cout*oh*ow 字节
 *
 * 节点超参（node 字段）：
 *   - fc_weights：权重 [Cout][Cin][Kh][Kw]，i8，行主序
 *   - fc_in_dim ：Cin（输入通道数）
 *   - fc_out_dim：Cout（输出通道数）
 *   - conv_kh/conv_kw：核高/宽，≥1
 *   - conv_sh/conv_sw：步长，≥1
 *   - conv_pad_top/bottom/left/right：explicit padding，0 = VALID
 *   - conv_bias：i32 偏置 [Cout]，可为 NULL
 *
 * @param node       图节点（含超参与权重指针）
 * @param in_tensor  输入 tensor，ndim==4，NCHW i8
 * @param out_tensor 输出 tensor，预分配缓冲需足够容纳结果
 * @return 0 成功；-1 参数无效或缓冲不足
 */
static int run_conv2d_node(const bm_tinyml_graph_node_t *node,
                           const bm_tinyml_tensor_t *in_tensor,
                           bm_tinyml_tensor_t *out_tensor) {
    uint32_t n_dim;
    uint32_t c_in_dim;
    uint32_t ih_dim;
    uint32_t iw_dim;
    uint32_t c_out_dim;
    uint32_t kh;
    uint32_t kw;
    uint32_t sh;
    uint32_t sw;
    uint32_t pad_top;
    uint32_t pad_bottom;
    uint32_t pad_left;
    uint32_t pad_right;
    uint32_t oh;
    uint32_t ow;
    uint32_t n;
    uint32_t oc;
    uint32_t ohy;
    uint32_t owx;
    uint32_t ic;
    uint32_t fh;
    uint32_t fw;

    if (node == NULL || in_tensor == NULL || out_tensor == NULL ||
        node->fc_weights == NULL || in_tensor->data == NULL ||
        out_tensor->data == NULL) {
        return -1;
    }
    if (in_tensor->ndim != 4u) {
        return -1;
    }

    n_dim    = in_tensor->dims[0];
    c_in_dim = in_tensor->dims[1];
    ih_dim   = in_tensor->dims[2];
    iw_dim   = in_tensor->dims[3];
    c_out_dim = node->fc_out_dim;
    kh       = node->conv_kh;
    kw       = node->conv_kw;
    sh       = node->conv_sh;
    sw       = node->conv_sw;
    pad_top    = node->conv_pad_top;
    pad_bottom = node->conv_pad_bottom;
    pad_left   = node->conv_pad_left;
    pad_right  = node->conv_pad_right;

    /* 参数合法性校验 */
    if (c_in_dim == 0u || c_out_dim == 0u || n_dim == 0u ||
        ih_dim == 0u || iw_dim == 0u ||
        kh == 0u || kw == 0u || sh == 0u || sw == 0u) {
        return -1;
    }
    if (c_in_dim != node->fc_in_dim) {
        return -1;
    }

    /* 输出尺寸：oh = (ih + pad_top + pad_bottom - kh) / sh + 1 */
    if ((ih_dim + pad_top + pad_bottom) < kh ||
        (iw_dim + pad_left + pad_right) < kw) {
        return -1;
    }
    oh = ((ih_dim + pad_top + pad_bottom) - kh) / sh + 1u;
    ow = ((iw_dim + pad_left + pad_right) - kw) / sw + 1u;
    if (oh == 0u || ow == 0u) {
        return -1;
    }

    /* 缓冲足够性校验 */
    if (in_tensor->byte_count < n_dim * c_in_dim * ih_dim * iw_dim ||
        out_tensor->byte_count < n_dim * c_out_dim * oh * ow) {
        return -1;
    }

    for (n = 0u; n < n_dim; ++n) {
        for (oc = 0u; oc < c_out_dim; ++oc) {
            for (ohy = 0u; ohy < oh; ++ohy) {
                for (owx = 0u; owx < ow; ++owx) {
                    int32_t acc = 0;
                    uint32_t out_idx;

                    /* 累加 bias（可选） */
                    if (node->conv_bias != NULL) {
                        acc = node->conv_bias[oc];
                    }

                    for (ic = 0u; ic < c_in_dim; ++ic) {
                        for (fh = 0u; fh < kh; ++fh) {
                            for (fw = 0u; fw < kw; ++fw) {
                                /* 输入坐标（加 padding 偏移后减去） */
                                uint32_t ih_raw = ohy * sh + fh;
                                uint32_t iw_raw = owx * sw + fw;
                                int8_t in_val;
                                uint32_t w_idx;

                                /* 越界像素用 zero_point 填充（等价数值 0） */
                                if (ih_raw < pad_top ||
                                    ih_raw >= ih_dim + pad_top ||
                                    iw_raw < pad_left ||
                                    iw_raw >= iw_dim + pad_left) {
                                    in_val = (int8_t)in_tensor->zero_point;
                                } else {
                                    uint32_t ihy = ih_raw - pad_top;
                                    uint32_t iwx = iw_raw - pad_left;
                                    uint32_t in_idx =
                                        ((n * c_in_dim + ic) * ih_dim + ihy) *
                                        iw_dim + iwx;
                                    in_val = in_tensor->data[in_idx];
                                }

                                /* 权重 layout: [oc][ic][fh][fw] 行主序 */
                                w_idx = ((oc * c_in_dim + ic) * kh + fh) *
                                        kw + fw;
                                acc += (int32_t)in_val *
                                       (int32_t)node->fc_weights[w_idx];
                            }
                        }
                    }

                    out_idx = ((n * c_out_dim + oc) * oh + ohy) * ow + owx;
                    out_tensor->data[out_idx] = clamp_i8(acc >> 7);
                }
            }
        }
    }

    out_tensor->ndim = 4u;
    out_tensor->dims[0] = n_dim;
    out_tensor->dims[1] = c_out_dim;
    out_tensor->dims[2] = oh;
    out_tensor->dims[3] = ow;
    out_tensor->byte_count = n_dim * c_out_dim * oh * ow;
    return 0;
}

int bm_tinyml_graph_init(bm_tinyml_graph_t *graph) {
    uint32_t i;

    if (graph == NULL || graph->nodes == NULL || graph->arena == NULL ||
        graph->tensors == NULL || graph->node_count == 0u ||
        graph->tensor_count == 0u) {
        return -1;
    }

    for (i = 0u; i < graph->node_count; ++i) {
        const bm_tinyml_graph_node_t *node = &graph->nodes[i];

        if (!graph_tensor_valid(graph, node->input_tensor) ||
            !graph_tensor_valid(graph, node->output_tensor)) {
            return -1;
        }
        if (node->op == BM_TINYML_OP_FC &&
            (node->fc_weights == NULL || node->fc_in_dim == 0u ||
             node->fc_out_dim == 0u)) {
            return -1;
        }
        if (node->op == BM_TINYML_OP_ADD &&
            !graph_tensor_valid(graph, node->input_tensor_b)) {
            return -1;
        }
        if (node->op == BM_TINYML_OP_MUL &&
            !graph_tensor_valid(graph, node->input_tensor_b)) {
            return -1;
        }
        if (node->op == BM_TINYML_OP_DEPTHWISE_CONV2D &&
            (node->fc_weights == NULL || node->fc_in_dim != 9u)) {
            return -1;
        }
        if (node->op == BM_TINYML_OP_CONV2D_1X1 &&
            (node->fc_weights == NULL || node->fc_in_dim == 0u ||
             node->fc_out_dim == 0u)) {
            return -1;
        }
        if (node->op == BM_TINYML_OP_CONV2D &&
            (node->fc_weights == NULL || node->fc_in_dim == 0u ||
             node->fc_out_dim == 0u ||
             node->conv_kh == 0u || node->conv_kw == 0u ||
             node->conv_sh == 0u || node->conv_sw == 0u)) {
            return -1;
        }
    }
    return 0;
}

int bm_tinyml_graph_run(bm_tinyml_graph_t *graph,
                        const float *float_inputs,
                        uint32_t float_input_count,
                        float *float_outputs,
                        uint32_t float_output_count) {
    uint32_t i;

    if (graph == NULL || bm_tinyml_graph_init(graph) != 0) {
        return -1;
    }

    for (i = 0u; i < graph->node_count; ++i) {
        const bm_tinyml_graph_node_t *node = &graph->nodes[i];
        bm_tinyml_tensor_t *in_tensor = &graph->tensors[node->input_tensor];
        bm_tinyml_tensor_t *out_tensor = &graph->tensors[node->output_tensor];

        switch (node->op) {
        case BM_TINYML_OP_QUANTIZE:
            if (float_inputs == NULL ||
                float_input_count < in_tensor->byte_count) {
                return -1;
            }
            if (bm_tinyml_tensor_quantize_f32(out_tensor, float_inputs,
                                              out_tensor->byte_count) != 0) {
                return -1;
            }
            break;
        case BM_TINYML_OP_FC:
            if (run_fc_node(node, in_tensor, out_tensor) != 0) {
                return -1;
            }
            break;
        case BM_TINYML_OP_DEQUANTIZE:
            if (float_outputs == NULL ||
                float_output_count < out_tensor->byte_count) {
                return -1;
            }
            if (bm_tinyml_tensor_dequantize_f32(in_tensor, float_outputs,
                                                out_tensor->byte_count) != 0) {
                return -1;
            }
            break;
        case BM_TINYML_OP_RELU:
            if (run_relu_node(in_tensor) != 0) {
                return -1;
            }
            if (out_tensor != in_tensor) {
                memcpy(out_tensor->data, in_tensor->data,
                       in_tensor->byte_count);
            }
            break;
        case BM_TINYML_OP_SOFTMAX:
            if (run_softmax_node(in_tensor) != 0) {
                return -1;
            }
            if (out_tensor != in_tensor && out_tensor->data != NULL) {
                memcpy(out_tensor->data, in_tensor->data,
                       in_tensor->byte_count);
            }
            break;
        case BM_TINYML_OP_FLATTEN:
            if (run_flatten_node(in_tensor, out_tensor) != 0) {
                return -1;
            }
            break;
        case BM_TINYML_OP_ADD: {
            const bm_tinyml_tensor_t *in_b =
                &graph->tensors[node->input_tensor_b];
            if (run_add_node(node, in_tensor, in_b, out_tensor) != 0) {
                return -1;
            }
            break;
        }
        case BM_TINYML_OP_MUL: {
            const bm_tinyml_tensor_t *in_b =
                &graph->tensors[node->input_tensor_b];
            if (run_mul_node(node, in_tensor, in_b, out_tensor) != 0) {
                return -1;
            }
            break;
        }
        case BM_TINYML_OP_MAXPOOL_2X2:
            if (run_maxpool_2x2_node(in_tensor, out_tensor) != 0) {
                return -1;
            }
            break;
        case BM_TINYML_OP_DEPTHWISE_CONV2D:
            if (run_depthwise_conv2d_node(node, in_tensor, out_tensor) != 0) {
                return -1;
            }
            break;
        case BM_TINYML_OP_CONV2D_1X1:
            if (run_conv2d_1x1_node(node, in_tensor, out_tensor) != 0) {
                return -1;
            }
            break;
        case BM_TINYML_OP_CONV2D:
            if (run_conv2d_node(node, in_tensor, out_tensor) != 0) {
                return -1;
            }
            break;
        default:
            return -1;
        }
    }
    return 0;
}
