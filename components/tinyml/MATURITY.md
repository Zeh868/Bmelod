# tinyml_adapter 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / 静态 bump arena + i8 tensor 量化 + 最小算子图

Not validated: 实机 TFLite Micro 运行时链接、DMA 零拷贝、完整 TFLM 模型导入

## 范围

- 编译期 `BM_TINYML_ARENA_MAX_BYTES`（默认 4096）静态 arena
- bump pointer 分配与 peak 统计
- tensor 描述符 + `bm_algo_features` 量化/反量化
- 最小算子图顺序执行：**quantize / fc / dequantize / relu / softmax / flatten / add / mul / maxpool_2x2 / depthwise_conv2d / conv2d_1x1**
- `tinyml_tflm_bridge.h`：TFLM BuiltinOperator 占位映射、POOL/DEPTHWISE/CONV 占位、arena 导出与 **5 步对接清单**（无 TFLM 库依赖）
- `tinyml_tflm_runtime.h/.c`：TFLM 运行时 E1 stub、`bm_tinyml_tflm_register_ops` 回调表；默认 `invoke` 返回 BM_ERR_NOT_SUPPORTED

## 已知限制

- 无 free/realloc，仅 reset 整池
- **不含 TFLM Interpreter**；`bm_tinyml_graph_run` 为 native_sim 对照实现
- **TFLM runtime 为 stub**；用户链接 TFLM 后通过 `bm_tinyml_tflm_register_ops` 替换实现
- FC 为 int8 矩阵乘 + >>7，非 TFLM 量化语义
- SOFTMAX 限 ≤16 元素、定点近似
- ADD/MUL 要求两输入 tensor 同 shape
- MAXPOOL_2X2 仅支持 2×2 stride-2、偶数 H/W、ndim=2（dims[0]=H, dims[1]=W）
- DEPTHWISE_CONV2D 仅支持 3×3 kernel、stride=1、pad=0、单通道；权重经 `fc_weights`/`fc_in_dim=9`
- CONV2D_1X1 仅支持 1×1、NCHW 展平（ndim=4）；权重 layout `[out_ch][in_ch]`，经 `fc_in_dim`/`fc_out_dim`
- tensor 最大 4 维，仅 int8 数据
