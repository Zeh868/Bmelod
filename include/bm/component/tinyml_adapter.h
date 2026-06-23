/**
 * @file tinyml_adapter.h
 * @brief TinyML 静态 arena 分配器与最小算子图
 *
 * bump pointer arena、tensor 元数据与 quantize/fc 顺序执行图（无 TFLM 依赖）。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            静态 arena 骨架
 * 2026-06-17       0.2            zeh            最小算子图 quantize/fc
 * 2026-06-17       0.3            zeh            ReLU 算子
 * 2026-06-17       0.4            zeh            SOFTMAX/FLATTEN 算子
 * 2026-06-17       0.5            zeh            ADD 双输入逐元素饱和相加
 * 2026-06-17       0.6            zeh            MUL 双输入逐元素饱和相乘
 * 2026-06-17       0.7            zeh            MAXPOOL 2x2 stride2 算子
 * 2026-06-17       0.8            zeh            DEPTHWISE 3x3 stride1 算子
 * 2026-06-17       0.9            zeh            CONV2D 1x1 NCHW 算子
 * 2026-06-23       1.0            zeh            通用 CONV2D（任意核/步长/padding）
 */
#ifndef BM_TINYML_ADAPTER_H
#define BM_TINYML_ADAPTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BM_TINYML_ARENA_MAX_BYTES
#define BM_TINYML_ARENA_MAX_BYTES  4096u
#endif

typedef struct {
    uint8_t  storage[BM_TINYML_ARENA_MAX_BYTES];
    uint32_t offset;
    uint32_t peak_bytes;
} bm_tinyml_arena_t;

typedef struct {
    int8_t  *data;
    uint32_t byte_count;
    uint32_t dims[4];
    uint32_t ndim;
    float    scale;
    int32_t  zero_point;
} bm_tinyml_tensor_t;

typedef struct {
    float scale;
    int32_t zero_point;
} bm_tinyml_quant_params_t;

typedef enum {
    BM_TINYML_OP_QUANTIZE = 0,
    BM_TINYML_OP_FC,
    BM_TINYML_OP_DEQUANTIZE,
    BM_TINYML_OP_RELU,
    BM_TINYML_OP_SOFTMAX,
    BM_TINYML_OP_FLATTEN,
    BM_TINYML_OP_ADD,
    BM_TINYML_OP_MUL,
    BM_TINYML_OP_MAXPOOL_2X2,
    BM_TINYML_OP_DEPTHWISE_CONV2D,
    BM_TINYML_OP_CONV2D_1X1,
    /** 通用标准 CONV2D：Cin→Cout，任意 KhxKw 核、可配 stride 与 explicit padding，NCHW i8 */
    BM_TINYML_OP_CONV2D
} bm_tinyml_op_t;

typedef struct {
    bm_tinyml_op_t  op;
    uint32_t        input_tensor;
    /** ADD/MUL 第二输入 tensor 索引；其他单输入算子忽略 */
    uint32_t        input_tensor_b;
    uint32_t        output_tensor;
    const int8_t   *fc_weights;
    uint32_t        fc_in_dim;
    uint32_t        fc_out_dim;
    /**
     * @name 通用 CONV2D 超参（BM_TINYML_OP_CONV2D 专用；其余算子置 0 不影响行为）
     *
     * - conv_kh / conv_kw：核高/宽，≥1
     * - conv_sh / conv_sw：步长高/宽，≥1
     * - conv_pad_top/bottom/left/right：explicit padding 像素数（VALID 时全 0）
     *   越界像素以 input zero_point 填充（等价于数值 0）
     *
     * 权重 layout：[out_ch][in_ch][kh][kw]，行主序；bias 可为 NULL（无 bias）。
     * @{
     */
    uint32_t        conv_kh;       /**< 卷积核高度 */
    uint32_t        conv_kw;       /**< 卷积核宽度 */
    uint32_t        conv_sh;       /**< 步长（高方向） */
    uint32_t        conv_sw;       /**< 步长（宽方向） */
    uint32_t        conv_pad_top;  /**< 顶部 padding 像素数 */
    uint32_t        conv_pad_bottom; /**< 底部 padding 像素数 */
    uint32_t        conv_pad_left; /**< 左侧 padding 像素数 */
    uint32_t        conv_pad_right;/**< 右侧 padding 像素数 */
    const int32_t  *conv_bias;     /**< 偏置数组 [out_ch]，i32，可为 NULL */
    /** @} */
} bm_tinyml_graph_node_t;

typedef struct {
    bm_tinyml_graph_node_t *nodes;
    uint32_t               node_count;
    bm_tinyml_arena_t     *arena;
    bm_tinyml_tensor_t    *tensors;
    uint32_t               tensor_count;
} bm_tinyml_graph_t;

void bm_tinyml_arena_reset(bm_tinyml_arena_t *arena);

uint32_t bm_tinyml_arena_bytes_used(const bm_tinyml_arena_t *arena);

void *bm_tinyml_arena_alloc(bm_tinyml_arena_t *arena,
                            uint32_t size,
                            uint32_t align);

int bm_tinyml_tensor_alloc_i8(bm_tinyml_arena_t *arena,
                              bm_tinyml_tensor_t *tensor,
                              const uint32_t *dims,
                              uint32_t ndim,
                              const bm_tinyml_quant_params_t *quant);

int bm_tinyml_tensor_quantize_f32(const bm_tinyml_tensor_t *tensor,
                                  const float *src,
                                  uint32_t count);

int bm_tinyml_tensor_dequantize_f32(const bm_tinyml_tensor_t *tensor,
                                    float *dst,
                                    uint32_t count);

/**
 * @brief 校验图节点与 tensor 索引
 *
 * @param graph 图描述（不可为 NULL）
 * @return 成功；参数无效
 */
int bm_tinyml_graph_init(bm_tinyml_graph_t *graph);

/**
 * @brief 按序执行图中算子
 *
 * @param graph 图描述（不可为 NULL）
 * @param float_inputs 量化节点源 float 缓冲（可为 NULL，若图无 QUANTIZE）
 * @param float_input_count float_inputs 元素数
 * @param float_outputs 反量化目标缓冲（可为 NULL，若图无 DEQUANTIZE）
 * @param float_output_count float_outputs 元素数
 * @return 成功；执行失败
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
