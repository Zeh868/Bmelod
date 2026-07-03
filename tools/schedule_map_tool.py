#!/usr/bin/env python3
"""schedule-map 级 2 复合分析：吃 N 份表 JSON（schema v1），出多核并列总表 +
全局超周期（跨表 LCM）+ 告警 + 可选 HTML 可视化。只汇总呈现，不重算单表事实
（防漂移铁律，唯一计算是跨表 LCM 与工作点线性缩放参考）。

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


def analyze(tables, op_hz_extra, warn_pct):
    """单一事实分析：为每张表算峰值格/占比/工作点缩放估计，收集告警文本，
    算跨表全局超周期。文本报告与 HTML 报告都调用这里，杜绝两套告警漂移。

    返回 (per_table, warns, global_hyper)，per_table 是列表，每项：
        {"peak": frame, "pct": float, "ops": [(f_hz, est, verdict), ...]}
    """
    per_table, warns = [], []
    for t in tables:
        peak = max(t["frames"], key=lambda f: f["isr_load_us"])
        pct = 100.0 * peak["isr_load_us"] / t["minor_us"]
        ops = []
        ref = t["ref_clk_hz"]
        if ref == 0:
            warns.append(f"WARN: ref_clk_hz 未声明 ({t['sched_name']}) —— 频率缩放分析跳过")
        else:
            op_hz_list = sorted(set(t.get("operating_points_hz", [])) | set(op_hz_extra))
            for f_hz in op_hz_list:
                if f_hz in (0, ref):
                    continue
                est = -(-peak["isr_load_us"] * ref // f_hz)  # ceil
                verdict = "OK" if est <= t["minor_us"] else "超载"
                ops.append((f_hz, est, verdict))
                if est > t["minor_us"]:
                    warns.append(f"WARN: [cpu{t['cpu']}] {t['sched_name']} @{f_hz}Hz "
                                 f"est 峰值 {est}us > minor {t['minor_us']}us (estimated)")
        if pct > warn_pct:
            warns.append(f"WARN: [cpu{t['cpu']}] {t['sched_name']} 峰值负载 {pct:.1f}% 超阈值 {warn_pct}%")
        per_table.append({"peak": peak, "pct": pct, "ops": ops})
    global_hyper = reduce(lcm, (t["hyperperiod_us"] for t in tables))
    return per_table, warns, global_hyper


def render(tables, op_hz_extra, warn_pct):
    per_table, warns, global_hyper = analyze(tables, op_hz_extra, warn_pct)
    lines = []
    lines.append("=== schedule-map 复合报告 (schema v1) ===")
    for t, a in zip(tables, per_table):
        peak, pct = a["peak"], a["pct"]
        cal = "已标定" if t["overhead_calibrated"] else "未标定占位"
        lines.append(f"[cpu{t['cpu']}] {t['sched_name']}: minor={t['minor_us']}us "
                     f"frames={t['n_frames']} hyper={t['hyperperiod_us']}us "
                     f"开销={t['overhead_us']}us[{cal}] "
                     f"峰值格 t={peak['t']} ({peak['isr_load_us']}us, {pct:.1f}% of minor)")
        for task in t["tasks"]:
            lines.append(f"  {task['domain'].upper():8s} {task['name']}  every={task['every']} "
                         f"at={task['at']} wcet={task['wcet_us']}us period={task['period_us']}us")
        for f_hz, est, verdict in a["ops"]:
            lines.append(f"  OP @{f_hz}Hz: est 峰值 {est}us / minor {t['minor_us']}us "
                         f"{verdict} (estimated)")
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
    width = n * (cell_w + gap) + gap
    height = cell_h + gap * 2 + 4
    peak_t = max(frames, key=lambda f: f["isr_load_us"])["t"] if frames else None
    parts = [f'<svg viewBox="0 0 {width} {height}" '
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
    width = n * (bar_w + gap) + gap
    height = plot_h + margin * 2
    max_val = max([minor] + [f["isr_load_us"] for f in frames]) or 1
    scale = plot_h / max_val
    ref_y = margin + plot_h - minor * scale
    parts = [f'<svg viewBox="0 0 {width} {height}" '
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
                     f'text-anchor="middle" font-size="10" fill="#111">{f["t"]}</text>')
    parts.append(f'<line x1="0" y1="{ref_y}" x2="{width}" y2="{ref_y}" '
                 f'stroke="{_COLOR_STROKE_PEAK}" stroke-width="1.5" stroke-dasharray="6,3"/>')
    parts.append(f'<text x="4" y="{ref_y - 4}" font-size="10" fill="#111">minor={minor}us</text>')
    parts.append("</svg>")
    return "".join(parts)


def _svg_load_stack(t):
    """负载图：isr_load_us 与 mainloop_pending_us 堆叠柱 + minor_us 参考线。"""
    frames = sorted(t["frames"], key=lambda f: f["t"])
    minor = t["minor_us"] or 1
    n = max(len(frames), 1)
    bar_w, gap, plot_h, margin = 40, 8, 160, 24
    width = n * (bar_w + gap) + gap
    height = plot_h + margin * 2
    max_val = max([minor] + [f["isr_load_us"] + f["mainloop_pending_us"] for f in frames]) or 1
    scale = plot_h / max_val
    ref_y = margin + plot_h - minor * scale
    parts = [f'<svg viewBox="0 0 {width} {height}" '
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
                     f'text-anchor="middle" font-size="10" fill="#111">{f["t"]}</text>')
    parts.append(f'<line x1="0" y1="{ref_y}" x2="{width}" y2="{ref_y}" '
                 f'stroke="{_COLOR_STROKE_PEAK}" stroke-width="1.5" stroke-dasharray="6,3"/>')
    parts.append(f'<text x="4" y="{ref_y - 4}" font-size="10" fill="#111">minor={minor}us</text>')
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


def render_html(tables, op_hz_extra, warn_pct):
    per_table, warns, global_hyper = analyze(tables, op_hz_extra, warn_pct)
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
        for f_hz, est, verdict in a["ops"]:
            parts.append(f'<div class="meta">OP @{f_hz}Hz: est 峰值 {est}us / minor '
                         f'{t["minor_us"]}us {verdict} (estimated)</div>')

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
    text = render(tables, args.op_hz, args.load_warn_pct)
    sys.stdout.write(text)
    if args.out:
        Path(args.out).write_text(text, encoding="utf-8")
    if args.html:
        try:
            Path(args.html).write_text(
                render_html(tables, args.op_hz, args.load_warn_pct), encoding="utf-8")
        except OSError as e:
            sys.stderr.write(f"schedule-map-tool: 写 HTML {args.html} 失败: {e}\n")
            sys.exit(2)
    sys.exit(0)


if __name__ == "__main__":
    main()
