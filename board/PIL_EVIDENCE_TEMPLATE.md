# PIL / HIL 证据归档模板（M5 脚手架）

填写处理器在环（PIL）或硬件在环（HIL）验收证据，与 `board/bm_board_envelope_stm32g4.h`、组件 `MATURITY.md` 对照。

## 基本信息

| 字段 | 填写 |
|------|------|
| 项目 / Demo | （例：motor_foc_sensored 电流环） |
| 板型 | （例：STM32G474RE Nucleo） |
| 环频率 / 块周期 | |
| 证据类型 | PIL / HIL / QEMU 周期回归 |
| 测量日期 | YYYY-MM-DD |
| 工具链 | （例：arm-none-eabi-gcc -O2） |

## 工况证据表

| ID | 工况描述 | 输入条件 | 期望结果 | 实测结果 | Pass/Fail | 备注 |
|----|----------|----------|----------|----------|-----------|------|
| P1 | 空载电流环阶跃 | Id_ref=0→0.5 pu | 跟踪误差 < 5% | | | |
| P2 | 编码器方向 | 正转 1 圈 | θ 单调增 | | | |
| P3 | MVDR 块流 | 2ch 正弦 1 kHz | 主瓣增益 > 旁瓣 6 dB | | | |

## QEMU 周期回归占位（非硬件 WCET）

使用 [`tools/measure_wcet.py`](../tools/measure_wcet.py) 记录可重复仿真周期，作为 CI/本地回归基线。输出 JSON 格式参见 [`reports/qemu_baseline.example.json`](reports/qemu_baseline.example.json)。

```powershell
python tools/measure_wcet.py --label stream_array_mvdr `
  qemu-system-arm -machine netduinoplus2 -kernel build/demo/stream_array_mvdr/firmware.elf -d exec,nochain 2>&1
```

| Label | 命令摘要 | 基线 cycles | 本次 cycles | 偏差 (%) | Pass/Fail | 日期 |
|-------|----------|-------------|-------------|----------|-----------|------|
| stream_array_mvdr | QEMU netduinoplus2 | （待填） | | | | |
| test_algorithm | native_sim 单元 | （待填） | | | | |

## 与 WCET 报告关系

- 实机 WCET 填写见 [WCET_REPORT_TEMPLATE.md](WCET_REPORT_TEMPLATE.md)
- PIL 证据侧重**功能与模型一致性**；WCET 侧重**时序预算**

## 结论

- [ ] 全部工况 Pass 或 Fail 已记录根因
- [ ] 已更新对应 `MATURITY.md` 与板级包络宏
- [ ] QEMU 回归基线已入库（可选）

**签署**：__________  **日期**：__________
