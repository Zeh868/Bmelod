# tinyml_graph

native_sim 最小 TinyML 算子图示例：`quantize → depthwise_conv2d(3×3) → dequantize`。

中心像素 10.0 经 3×3 单通道 depthwise 核（中心权重 127）后输出约 9.0（int8 >>7 定点）。

## 依赖

- `BM_ENABLE_ALGORITHM=ON`
- `BM_ENABLE_COMPONENT_TINYML=ON`

## 构建（native_sim）

```powershell
cmake -B build/demo_batch12 -S Demo -DBMELOD_DEMO_NATIVE=ON
cmake --build build/demo_batch12 --target tinyml_graph --config Debug
.\build\demo_batch12\tinyml_graph.exe
```

## 输出

打印 dequant 浮点输出与 arena `peak_bytes`，成功时输出 `EXAMPLE_TINYML_GRAPH: PASS`。
