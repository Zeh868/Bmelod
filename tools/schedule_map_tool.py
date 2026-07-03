#!/usr/bin/env python3
"""schedule-map 级 2 复合分析：吃 N 份表 JSON（schema v1），出多核并列总表 +
全局超周期（跨表 LCM）+ 告警 + 可选 HTML 可视化。只汇总呈现，不重算单表事实
（防漂移铁律，唯一计算是跨表 LCM 与工作点线性缩放参考）。

每张表若只有 0/1 个频率点（或 ref_clk_hz==0）出一张表（单表模式）；若有多个
频率点（ref_clk_hz ∪ 表自带 operating_points_hz ∪ --op-hz 的并集），按
ref_clk_hz 锚点线性外推，每个频率各出一张完整分析表 + 一张频率对比总表
（多表模式），频率点数上限 MAX_OP_POINTS=8，超出截断并 stderr 告警。

用法: schedule_map_tool.py (--dir D | files...) [--out F] [--html F] [--op-hz N]... [--load-warn-pct 80]
退出码: 0=成功(允许 WARN)  2=schema/IO 错误
"""
import argparse, html as _html, json, math, sys
from functools import reduce
from pathlib import Path

# Windows 控制台默认区域编码（如 GBK/cp936）与本工具的 UTF-8 中文输出不一致，
# 显式锁定 stdout/stderr 为 UTF-8，避免跨平台/跨区域设置乱码或解码异常。
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8")

SCHEMA = 1
# 单表参与频率分析的工作点上限：超过会撑爆报告篇幅，且 cmake/bm_schedule_map.cmake
# 里 OPERATING_POINTS 本身也钉了同一上限（构建期防呆），两处数值必须一致。
MAX_OP_POINTS = 8


def load(path):
    try:
        d = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        sys.stderr.write(f"schedule-map-tool: 读 {path} 失败: {e}\n")
        sys.exit(2)
    if d.get("schema_version") != SCHEMA:
        sys.stderr.write(f"schedule-map-tool: {path} schema_version 不是 {SCHEMA}\n")
        sys.exit(2)
    return d


def lcm(a, b):
    return a * b // math.gcd(a, b)


def _scale_ceil(value, ref, f_hz):
    """按锚点频率线性外推：ceil(value × ref / f_hz)。f_hz 等于 ref 时原样返回
    （那一档是声明值本身，不缩放）。唯一新增计算，其余事实一律照抄单表 JSON。"""
    if f_hz == ref:
        return value
    return -(-value * ref // f_hz)


def _interference(sources, minor_us, ref=None, f_hz=None):
    """算一组干扰源对某 minor 窗口的 ceiling 上界抢占：
    total = Σ ceil(minor_us / period_us) × wcet_us（wcet 可选按 ref/f 缩放）。
    period_us<=0 视为声明错误，计入 bad 并跳过（不除零）。按 tier 分别累计。"""
    acc = {"hardware": 0, "scheduled": 0, "total": 0, "bad": []}
    for s in sources:
        period = int(s.get("period_us", 0))
        if period <= 0:
            acc["bad"].append(s.get("name", "?"))
            continue
        wcet = int(s.get("wcet_us", 0))
        if ref and f_hz and f_hz != ref:
            wcet = _scale_ceil(wcet, ref, f_hz)
        contrib = -(-minor_us // period) * wcet  # ceil(minor/period) × wcet
        tier = s.get("tier", "scheduled")
        key = "hardware" if tier == "hardware" else "scheduled"
        acc[key] += contrib
        acc["total"] += contrib
    return acc


def _table_freqs(t, op_hz_extra):
    """算某张表参与频率分析的全部工作点：ref_clk_hz ∪ 表自带 operating_points_hz ∪
    命令行 --op-hz，剔除 0，排序去重。"""
    ref = t.get("ref_clk_hz", 0)
    freqs = set(t.get("operating_points_hz", [])) | set(op_hz_extra)
    if ref:
        freqs.add(ref)
    freqs.discard(0)
    return sorted(freqs)


def _cap_freqs(freqs, ref):
    """频率点数超过 MAX_OP_POINTS 时截断到前 MAX_OP_POINTS 个，务必保留 ref_clk_hz
    那一档（缺了它就没法当缩放锚点）。返回 (freqs_capped, was_capped)。"""
    if len(freqs) <= MAX_OP_POINTS:
        return freqs, False
    capped = list(freqs[:MAX_OP_POINTS])
    if ref and ref not in capped:
        capped[-1] = ref
        capped = sorted(capped)
    return capped, True


def analyze(tables, op_hz_extra, warn_pct):
    """单一事实分析：为每张表算峰值格/占比，单频率表保持现状，多频率表按
    ref_clk_hz 锚点线性外推出每个频率档的完整分析 + 收集告警文本，算跨表全局
    超周期。文本报告与 HTML 报告都调用这里，杜绝两套告警漂移。

    返回 (per_table, warns, global_hyper)，per_table 是列表，每项：
        {"peak": frame, "pct": float, "mode": "single"|"multi",
         "freq_tables": [{"f_hz", "is_ref", "tasks", "peak_t", "peak_us",
                           "pct", "feasible", "intf", "eff_peak_us",
                           "eff_pct"}, ...]}
    每个频率档的 feasible 按 eff_peak_us(=peak_us+intf.total) 判定，intf 由
    _interference 按该档 ref/f_hz 缩放 wcet 算出（period 不缩放）。
    mode=="single" 时 freq_tables 为空列表，沿用原单表输出。
    """
    per_table, warns = [], []
    for t in tables:
        ref = t.get("ref_clk_hz", 0)
        minor = t["minor_us"]
        raw_peak = max(t["frames"], key=lambda f: f["isr_load_us"])
        raw_pct = 100.0 * raw_peak["isr_load_us"] / minor
        entry = {"peak": raw_peak, "pct": raw_pct, "mode": "single", "freq_tables": []}

        intf = _interference(t.get("interference_sources", []), minor)
        eff_peak = raw_peak["isr_load_us"] + intf["total"]
        entry["intf"] = intf
        entry["eff_peak_us"] = eff_peak
        entry["eff_pct"] = 100.0 * eff_peak / minor
        entry["feasible"] = eff_peak <= minor
        for bad in intf["bad"]:
            warns.append(f"WARN: [cpu{t['cpu']}] {t['sched_name']} 干扰源 {bad} period_us<=0，已跳过")
        if t.get("interference_sources"):
            if intf["total"] >= minor:
                warns.append(f"WARN: [cpu{t['cpu']}] {t['sched_name']} 干扰单独 {intf['total']}us ≥ minor {minor}us（TT 无预算）")
            elif not entry["feasible"]:
                warns.append(f"WARN: [cpu{t['cpu']}] {t['sched_name']} 计入干扰后有效峰值 {eff_peak}us > minor {minor}us")

        if ref == 0:
            if t.get("operating_points_hz"):
                warns.append(f"WARN: {t['sched_name']} 声明了 DVFS 点但缺 "
                             f"BM_CONFIG_CPU_FREQ_HZ 锚点，无法频率缩放（退为单表）")
            else:
                warns.append(f"WARN: ref_clk_hz 未声明 ({t['sched_name']}) —— 频率缩放分析跳过")
        else:
            freqs = _table_freqs(t, op_hz_extra)
            if len(freqs) > 1:
                freqs, capped = _cap_freqs(freqs, ref)
                if capped:
                    sys.stderr.write(
                        f"schedule-map-tool: [cpu{t['cpu']}] {t['sched_name']} "
                        f"工作点频率数超过上限，最多 8 个，已截断（保留基准频率）\n")
                entry["mode"] = "multi"
                for f_hz in freqs:
                    scaled_frames = [
                        {"t": fr["t"], "isr_load_us": _scale_ceil(fr["isr_load_us"], ref, f_hz)}
                        for fr in t["frames"]
                    ]
                    peak_fr = max(scaled_frames, key=lambda fr: fr["isr_load_us"])
                    pct = 100.0 * peak_fr["isr_load_us"] / minor
                    # 该频率档的干扰上界：period 不缩放，wcet 经 ref/f_hz 缩放
                    # （_interference 内部处理），计入后才是有效峰值/可行性判定。
                    intf_f = _interference(t.get("interference_sources", []), minor, ref, f_hz)
                    eff = peak_fr["isr_load_us"] + intf_f["total"]
                    feasible = eff <= minor
                    tasks_scaled = [
                        dict(task, wcet_us=_scale_ceil(task["wcet_us"], ref, f_hz))
                        for task in t["tasks"]
                    ]
                    entry["freq_tables"].append({
                        "f_hz": f_hz, "is_ref": f_hz == ref, "tasks": tasks_scaled,
                        "peak_t": peak_fr["t"], "peak_us": peak_fr["isr_load_us"],
                        "pct": pct, "feasible": feasible,
                        "intf": intf_f, "eff_peak_us": eff, "eff_pct": 100.0 * eff / minor,
                    })
                    if f_hz != ref and not feasible:
                        # opt-in：未声明干扰源时沿用原文案逐字不变；声明了才
                        # 换成计入干扰的文案，避免空干扰场景下的输出漂移。
                        if t.get("interference_sources"):
                            warns.append(
                                f"WARN: [cpu{t['cpu']}] {t['sched_name']} @{f_hz}Hz "
                                f"有效峰值 {eff}us > minor {minor}us (含干扰, estimated)")
                        else:
                            warns.append(
                                f"WARN: [cpu{t['cpu']}] {t['sched_name']} @{f_hz}Hz "
                                f"est 峰值 {peak_fr['isr_load_us']}us > minor {minor}us (estimated)")
        if raw_pct > warn_pct:
            warns.append(f"WARN: [cpu{t['cpu']}] {t['sched_name']} 峰值负载 {raw_pct:.1f}% 超阈值 {warn_pct}%")
        per_table.append(entry)
    global_hyper = reduce(lcm, (t["hyperperiod_us"] for t in tables))
    return per_table, warns, global_hyper


def render(tables, per_table, warns, global_hyper):
    lines = []
    lines.append("=== schedule-map 复合报告 (schema v1) ===")
    for t, a in zip(tables, per_table):
        cal = "已标定" if t["overhead_calibrated"] else "未标定占位"
        lines.append(f"[cpu{t['cpu']}] {t['sched_name']}: minor={t['minor_us']}us "
                     f"frames={t['n_frames']} hyper={t['hyperperiod_us']}us "
                     f"开销={t['overhead_us']}us[{cal}]")
        if a["mode"] == "single":
            peak, pct = a["peak"], a["pct"]
            lines.append(f"  峰值格 t={peak['t']} ({peak['isr_load_us']}us, {pct:.1f}% of minor)")
            if t.get("interference_sources"):
                intf = a["intf"]
                verdict = "排得下 ✓" if a["feasible"] else "超载 ✗"
                lines.append(f"  干扰(硬{intf['hardware']}/调{intf['scheduled']})={intf['total']}us  "
                             f"有效峰值={a['eff_peak_us']}us ({a['eff_pct']:.1f}% of minor) {verdict}")
            for task in t["tasks"]:
                lines.append(f"  {task['domain'].upper():8s} {task['name']}  every={task['every']} "
                             f"at={task['at']} wcet={task['wcet_us']}us period={task['period_us']}us")
        else:
            for ft in a["freq_tables"]:
                tag = "基准/声明" if ft["is_ref"] else "理论/estimated"
                verdict = "排得下 ✓" if ft["feasible"] else "超载 ✗"
                lines.append(f"  --- 频率档 @{ft['f_hz']}Hz（{tag}） ---")
                for task in ft["tasks"]:
                    lines.append(f"    {task['domain'].upper():8s} {task['name']}  every={task['every']} "
                                 f"at={task['at']} wcet={task['wcet_us']}us period={task['period_us']}us")
                lines.append(f"    峰值格 t={ft['peak_t']} ({ft['peak_us']}us, {ft['pct']:.1f}% of minor) {verdict}")
                if t.get("interference_sources"):
                    it = ft["intf"]
                    v2 = "排得下 ✓" if ft["feasible"] else "超载 ✗"
                    lines.append(f"    干扰(硬{it['hardware']}/调{it['scheduled']})={it['total']}us  "
                                 f"有效={ft['eff_peak_us']}us ({ft['eff_pct']:.1f}%) {v2}")
            lines.append("  频率对比总表:")
            for ft in a["freq_tables"]:
                tag = "基准/声明" if ft["is_ref"] else "理论/estimated"
                verdict = "排得下 ✓" if ft["feasible"] else "超载 ✗"
                if t.get("interference_sources"):
                    lines.append(f"    {ft['f_hz']}Hz（{tag}）  有效={ft['eff_peak_us']}us（含干扰{ft['intf']['total']}）  "
                                 f"{ft['eff_pct']:.1f}%  {verdict}")
                else:
                    lines.append(f"    {ft['f_hz']}Hz（{tag}）  峰值={ft['peak_us']}us  "
                                 f"{ft['pct']:.1f}%  {verdict}")
            lines.append("  注: 以上理论值按 wcet×ref/f 线性外推(estimated)，需上板实测验证")
        if t.get("interference_sources"):
            n = len(t["interference_sources"])
            lines.append(f"  注: 已计入声明干扰源（{n} 个 HRT 抢占，ceiling 上界；未声明的仍在账外）")
        else:
            lines.append("  注: 本表仅含 TT 门面负载，账外中断/slot 不在内")
    parts = ", ".join(str(t["hyperperiod_us"]) for t in tables)
    lines.append(f"全局超周期: {global_hyper}us = LCM({parts})")
    lines.extend(warns)
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# HTML 可视化：全部内联 <style>/<svg>，零外链，只画 JSON 里已有的字段
# （frames 的 t/isr_load_us/mainloop_pending_us、minor_us、tasks、元信息）。
# ---------------------------------------------------------------------------

def _esc(s):
    return _html.escape(str(s), quote=True)


# 三档语义色：正常 / 近阈值 / 超载。中性描边保证浅底深底都可读。
_COLOR_OK = "#3d9970"
_COLOR_WARN = "#e8a33d"
_COLOR_OVER = "#d9534f"
_COLOR_STROKE = "#666666"
_COLOR_STROKE_PEAK = "#111111"


def _load_color(pct):
    if pct >= 100.0:
        return _COLOR_OVER
    if pct >= 80.0:
        return _COLOR_WARN
    return _COLOR_OK


def _svg_frame_strip(t):
    """时间格视图：一排 n_frames 个矩形，按 isr_load_us/minor_us 占比着色，
    格内标 t 与 isr_load_us，峰值格描边高亮。"""
    frames = sorted(t["frames"], key=lambda f: f["t"])
    minor = t["minor_us"] or 1
    n = max(len(frames), 1)
    cell_w, cell_h, gap = 60, 60, 4
    width = max(n * (cell_w + gap) + gap, 120)
    height = cell_h + gap * 2 + 4
    peak_t = max(frames, key=lambda f: f["isr_load_us"])["t"] if frames else None
    parts = [f'<svg width="{width}" height="{height}" viewBox="0 0 {width} {height}" '
             f'role="img" aria-label="时间格视图">']
    for i, f in enumerate(frames):
        pct = 100.0 * f["isr_load_us"] / minor
        x = gap + i * (cell_w + gap)
        y = gap
        fill = _load_color(pct)
        stroke = _COLOR_STROKE_PEAK if f["t"] == peak_t else _COLOR_STROKE
        sw = 2.5 if f["t"] == peak_t else 1
        parts.append(f'<rect x="{x}" y="{y}" width="{cell_w}" height="{cell_h}" '
                     f'fill="{fill}" stroke="{stroke}" stroke-width="{sw}" rx="3"/>')
        parts.append(f'<text x="{x + cell_w / 2}" y="{y + cell_h / 2 - 6}" '
                     f'text-anchor="middle" font-size="11" fill="#111">t={f["t"]}</text>')
        parts.append(f'<text x="{x + cell_w / 2}" y="{y + cell_h / 2 + 12}" '
                     f'text-anchor="middle" font-size="11" fill="#111">{f["isr_load_us"]}us</text>')
    parts.append("</svg>")
    return "".join(parts)


def _svg_wcet_bars(t):
    """每拍 WCET 图：isr_load_us 柱状图 + minor_us 参考线，超线柱变红。"""
    frames = sorted(t["frames"], key=lambda f: f["t"])
    minor = t["minor_us"] or 1
    n = max(len(frames), 1)
    bar_w, gap, plot_h, margin = 40, 8, 160, 24
    width = max(n * (bar_w + gap) + gap, 260)
    height = plot_h + margin * 2
    max_val = max([minor] + [f["isr_load_us"] for f in frames]) or 1
    scale = plot_h / max_val
    ref_y = margin + plot_h - minor * scale
    parts = [f'<svg width="{width}" height="{height}" viewBox="0 0 {width} {height}" '
             f'role="img" aria-label="每拍 WCET 图">']
    for i, f in enumerate(frames):
        x = gap + i * (bar_w + gap)
        h = f["isr_load_us"] * scale
        y = margin + plot_h - h
        over = f["isr_load_us"] > minor
        fill = _COLOR_OVER if over else _COLOR_OK
        parts.append(f'<rect x="{x}" y="{y}" width="{bar_w}" height="{h}" '
                     f'fill="{fill}" stroke="{_COLOR_STROKE}" stroke-width="1"/>')
        parts.append(f'<text x="{x + bar_w / 2}" y="{margin + plot_h + 14}" '
                     f'text-anchor="middle" font-size="10" fill="currentColor" opacity="0.85">{f["t"]}</text>')
    parts.append(f'<line x1="0" y1="{ref_y}" x2="{width}" y2="{ref_y}" '
                 f'stroke="currentColor" stroke-width="1.5" stroke-dasharray="6,3" opacity="0.75"/>')
    parts.append(f'<text x="4" y="{ref_y - 4}" font-size="10" fill="currentColor" opacity="0.85">minor={minor}us</text>')
    parts.append("</svg>")
    return "".join(parts)


def _svg_load_stack(t):
    """负载图：isr_load_us 与 mainloop_pending_us 堆叠柱 + minor_us 参考线。"""
    frames = sorted(t["frames"], key=lambda f: f["t"])
    minor = t["minor_us"] or 1
    n = max(len(frames), 1)
    bar_w, gap, plot_h, margin = 40, 8, 160, 24
    width = max(n * (bar_w + gap) + gap, 260)
    height = plot_h + margin * 2
    max_val = max([minor] + [f["isr_load_us"] + f["mainloop_pending_us"] for f in frames]) or 1
    scale = plot_h / max_val
    ref_y = margin + plot_h - minor * scale
    parts = [f'<svg width="{width}" height="{height}" viewBox="0 0 {width} {height}" '
             f'role="img" aria-label="负载图">']
    for i, f in enumerate(frames):
        x = gap + i * (bar_w + gap)
        h_isr = f["isr_load_us"] * scale
        h_main = f["mainloop_pending_us"] * scale
        y_isr = margin + plot_h - h_isr
        y_main = y_isr - h_main
        parts.append(f'<rect x="{x}" y="{y_isr}" width="{bar_w}" height="{h_isr}" '
                     f'fill="{_COLOR_OK}" stroke="{_COLOR_STROKE}" stroke-width="1"/>')
        parts.append(f'<rect x="{x}" y="{y_main}" width="{bar_w}" height="{h_main}" '
                     f'fill="#4a7fd6" stroke="{_COLOR_STROKE}" stroke-width="1"/>')
        parts.append(f'<text x="{x + bar_w / 2}" y="{margin + plot_h + 14}" '
                     f'text-anchor="middle" font-size="10" fill="currentColor" opacity="0.85">{f["t"]}</text>')
    parts.append(f'<line x1="0" y1="{ref_y}" x2="{width}" y2="{ref_y}" '
                 f'stroke="currentColor" stroke-width="1.5" stroke-dasharray="6,3" opacity="0.75"/>')
    parts.append(f'<text x="4" y="{ref_y - 4}" font-size="10" fill="currentColor" opacity="0.85">minor={minor}us</text>')
    parts.append("</svg>")
    return "".join(parts)


_STYLE = """
:root { color-scheme: light dark; }
body { font-family: -apple-system, "Segoe UI", "Microsoft YaHei", sans-serif;
       margin: 0; padding: 24px; background: #f5f5f7; color: #1a1a1a; }
@media (prefers-color-scheme: dark) {
  body { background: #1a1a1c; color: #e8e8ea; }
  section, .overview { background: #26262a !important; border-color: #3a3a3e !important; }
  .warn { background: #4a3410 !important; color: #f0c674 !important; }
}
h1 { font-size: 20px; }
h2 { font-size: 16px; margin-top: 0; }
h3 { font-size: 13px; color: #666; margin-bottom: 4px; }
.overview { background: #fff; border: 1px solid #ddd; border-radius: 8px; padding: 16px; margin-bottom: 20px; }
section { background: #fff; border: 1px solid #ddd; border-radius: 8px; padding: 16px; margin-bottom: 20px; }
.warn { background: #fdf0e0; color: #8a5a00; padding: 6px 10px; border-radius: 4px;
        margin: 4px 0; font-size: 13px; }
table { border-collapse: collapse; font-size: 13px; margin: 8px 0; }
td, th { padding: 3px 10px; text-align: left; border-bottom: 1px solid #eee; }
.svg-wrap { overflow-x: auto; margin: 8px 0; }
.svg-wrap svg { height: auto; max-width: 100%; }
.meta { font-size: 13px; color: #555; }
"""


def render_html(tables, per_table, warns, global_hyper):
    parts = ['<!doctype html>', '<html lang="zh-CN"><head><meta charset="utf-8">',
             f'<title>schedule-map 复合报告</title><style>{_STYLE}</style></head><body>']
    parts.append('<h1>schedule-map 复合报告 (schema v1)</h1>')

    parts.append('<div class="overview">')
    hyper_parts = ", ".join(str(t["hyperperiod_us"]) for t in tables)
    parts.append(f'<div class="meta">全局超周期: <b>{global_hyper}us</b> = LCM({_esc(hyper_parts)})</div>')
    parts.append('<table><tr><th>cpu</th><th>sched_name</th><th>峰值格</th><th>峰值占比</th></tr>')
    for t, a in zip(tables, per_table):
        parts.append(f'<tr><td>{t["cpu"]}</td><td>{_esc(t["sched_name"])}</td>'
                     f'<td>t={a["peak"]["t"]} ({a["peak"]["isr_load_us"]}us)</td>'
                     f'<td>{a["pct"]:.1f}%</td></tr>')
    parts.append('</table>')
    if warns:
        for w in warns:
            parts.append(f'<div class="warn">{_esc(w)}</div>')
    parts.append('</div>')

    for t, a in zip(tables, per_table):
        cal = "已标定" if t["overhead_calibrated"] else "未标定占位"
        parts.append('<section>')
        parts.append(f'<h2>[cpu{t["cpu"]}] {_esc(t["sched_name"])}</h2>')
        parts.append(f'<div class="meta">minor={t["minor_us"]}us frames={t["n_frames"]} '
                     f'hyper={t["hyperperiod_us"]}us 开销={t["overhead_us"]}us[{cal}] '
                     f'峰值格 t={a["peak"]["t"]} ({a["peak"]["isr_load_us"]}us, {a["pct"]:.1f}% of minor)</div>')
        parts.append('<table><tr><th>domain</th><th>name</th><th>every</th><th>at</th>'
                     '<th>wcet_us</th><th>period_us</th></tr>')
        for task in t["tasks"]:
            parts.append(f'<tr><td>{_esc(task["domain"])}</td><td>{_esc(task["name"])}</td>'
                         f'<td>{task["every"]}</td><td>{task["at"]}</td>'
                         f'<td>{task["wcet_us"]}</td><td>{task["period_us"]}</td></tr>')
        parts.append('</table>')
        if a["mode"] == "multi":
            parts.append('<h3>频率对比表</h3>')
            parts.append('<table><tr><th>频率</th><th>峰值</th><th>峰值占比</th><th>可行性</th></tr>')
            for ft in a["freq_tables"]:
                tag = "基准/声明" if ft["is_ref"] else "理论/estimated"
                verdict = "排得下 ✓" if ft["feasible"] else "超载 ✗"
                parts.append(f'<tr><td>{ft["f_hz"]}Hz（{_esc(tag)}）</td>'
                             f'<td>{ft["peak_us"]}us</td><td>{ft["pct"]:.1f}%</td>'
                             f'<td>{_esc(verdict)}</td></tr>')
            parts.append('</table>')
            parts.append('<div class="meta">理论值按 wcet×ref/f 线性外推(estimated)，'
                         '需上板实测验证；下方三图按基准频率绘制（负载随频率线性缩放，结构不变，'
                         '基准图足够代表，不逐档重画）</div>')

        parts.append('<h3>时间格视图</h3>')
        parts.append(f'<div class="svg-wrap">{_svg_frame_strip(t)}</div>')
        parts.append('<h3>每拍 WCET 图</h3>')
        parts.append(f'<div class="svg-wrap">{_svg_wcet_bars(t)}</div>')
        parts.append('<h3>负载图（isr_load_us + mainloop_pending_us 堆叠, minor_us 参考线）</h3>')
        parts.append(f'<div class="svg-wrap">{_svg_load_stack(t)}</div>')
        parts.append('</section>')

    parts.append('</body></html>')
    return "\n".join(parts) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="*")
    ap.add_argument("--dir")
    ap.add_argument("--out")
    ap.add_argument("--html")
    ap.add_argument("--op-hz", type=int, action="append", default=[])
    ap.add_argument("--load-warn-pct", type=int, default=80)
    args = ap.parse_args()
    files = list(args.files)
    if args.dir:
        files += sorted(str(p) for p in Path(args.dir).glob("*.json"))
    if not files:
        sys.stderr.write("schedule-map-tool: 没有输入 JSON\n")
        sys.exit(2)
    tables = sorted((load(f) for f in files), key=lambda t: (t["cpu"], t["sched_name"]))
    per_table, warns, global_hyper = analyze(tables, args.op_hz, args.load_warn_pct)
    text = render(tables, per_table, warns, global_hyper)
    sys.stdout.write(text)
    if args.out:
        Path(args.out).write_text(text, encoding="utf-8")
    if args.html:
        try:
            Path(args.html).write_text(
                render_html(tables, per_table, warns, global_hyper), encoding="utf-8")
        except OSError as e:
            sys.stderr.write(f"schedule-map-tool: 写 HTML {args.html} 失败: {e}\n")
            sys.exit(2)
    sys.exit(0)


if __name__ == "__main__":
    main()
