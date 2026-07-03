#!/usr/bin/env python3
"""schedule_map_tool 自测：构造 JSON → 总表/全局 LCM/告警/退出码/HTML/
按频率分表（多频率各出一张理论分析表 + 频率对比总表，上限 8 档截断）。"""
import json, os, subprocess, sys, tempfile, unittest

TOOL = os.path.join(os.path.dirname(__file__), "..", "..", "tools", "schedule_map_tool.py")

import importlib.util as _ilu
_spec = _ilu.spec_from_file_location("smt", TOOL)
smt = _ilu.module_from_spec(_spec); _spec.loader.exec_module(smt)

def make_a():
    return {
        "schema_version": 1, "sched_name": "sched_fixture_a", "cpu": 0,
        "minor_us": 1000, "n_frames": 10, "hyperperiod_us": 10000,
        "overhead_us": 0, "overhead_calibrated": False,
        "ref_clk_hz": 240000000, "operating_points_hz": [240000000, 80000000],
        "tasks": [
            {"name": "fast", "every": 1, "at": 0, "wcet_us": 50, "domain": "isr",
             "kind": "compute", "period_us": 1000, "deadline_us": 1000, "inputs": 1, "outputs": 1},
        ],
        "frames": [{"t": t, "isr_load_us": 100 if t in (0, 5, 9) else 50,
                    "mainloop_pending_us": 200 if t == 0 else 0} for t in range(10)],
        "edges": [],
    }

def make_b():
    d = make_a()
    d.update(sched_name="sched_fixture_b", cpu=1, minor_us=2000, n_frames=2,
             hyperperiod_us=4000, ref_clk_hz=0, operating_points_hz=[],
             frames=[{"t": 0, "isr_load_us": 0, "mainloop_pending_us": 0},
                     {"t": 1, "isr_load_us": 80, "mainloop_pending_us": 0}])
    return d

def run_tool(tmp, *extra):
    files = []
    for d in (make_a(), make_b()):
        p = os.path.join(tmp, d["sched_name"] + ".json")
        with open(p, "w", encoding="utf-8") as f:
            json.dump(d, f)
        files.append(p)
    return subprocess.run([sys.executable, TOOL, *files, *extra],
                          capture_output=True, text=True, encoding="utf-8")

class T(unittest.TestCase):
    def test_composite_and_lcm(self):
        with tempfile.TemporaryDirectory() as tmp:
            r = run_tool(tmp)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn("全局超周期: 20000us", r.stdout)
            self.assertIn("sched_fixture_a", r.stdout)
            self.assertIn("[cpu1]", r.stdout)

    def test_warnings(self):
        with tempfile.TemporaryDirectory() as tmp:
            r = run_tool(tmp)
            self.assertIn("WARN", r.stdout)
            self.assertIn("estimated", r.stdout)

    def test_multi_freq_table_scaling(self):
        """make_a 声明两个工作点（240M 基准 + 80M）→ 多频率模式：每个频率各出
        一张完整分析表 + 一张频率对比总表，理论值按 wcet×ref/f 线性外推。"""
        with tempfile.TemporaryDirectory() as tmp:
            r = run_tool(tmp)
            self.assertEqual(r.returncode, 0, r.stderr)
            out = r.stdout
            # 基准档 240000000Hz：声明值本身，不缩放
            self.assertIn("240000000Hz", out)
            self.assertIn("基准", out)
            self.assertIn("(100us, 10.0% of minor) 排得下", out)
            # 理论档 80000000Hz：ceil(100 * 240 / 80) = 300us
            self.assertIn("80000000Hz", out)
            self.assertIn("理论/estimated", out)
            self.assertIn("(300us, 30.0% of minor) 排得下", out)
            # 频率对比总表两行都要出现
            self.assertIn("频率对比总表", out)
            self.assertIn("峰值=100us", out)
            self.assertIn("峰值=300us", out)
            self.assertIn("实测验证", out)

    def test_single_freq_table_unaffected(self):
        """make_b ref_clk_hz==0 → 单表模式，不出各频率分表/频率对比总表。"""
        with tempfile.TemporaryDirectory() as tmp:
            r = run_tool(tmp)
            self.assertEqual(r.returncode, 0, r.stderr)
            b_section = r.stdout.split("[cpu1]", 1)[1]
            self.assertNotIn("频率档 @", b_section)
            self.assertNotIn("频率对比总表", b_section)

    def test_op_scaling_overload_warn(self):
        with tempfile.TemporaryDirectory() as tmp:
            r = run_tool(tmp, "--op-hz", "20000000")
            self.assertEqual(r.returncode, 0)
            # 新增 20000000Hz 档：ceil(100*240/20)=1200us > minor 1000us → 超载
            self.assertIn("WARN: [cpu0] sched_fixture_a @20000000Hz est 峰值 1200us > minor 1000us", r.stdout)
            self.assertIn("(1200us, 120.0% of minor) 超载", r.stdout)
            self.assertIn("超载", r.stdout)
            # 对比总表该行也要标超载
            self.assertIn("20000000Hz（理论/estimated）  峰值=1200us  120.0%  超载", r.stdout)

    def test_op_points_cap_at_8(self):
        """频率点数超过 MAX_OP_POINTS(8) 时 stderr 警告 + 截断到 8 档（保留
        ref_clk），退出码仍是 0。"""
        with tempfile.TemporaryDirectory() as tmp:
            args = []
            for v in (10000000, 20000000, 30000000, 40000000, 50000000, 60000000, 70000000, 90000000):
                args += ["--op-hz", str(v)]
            r = run_tool(tmp, *args)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn("最多 8", r.stderr)
            self.assertIn("sched_fixture_a", r.stderr)
            # cpu0 (sched_fixture_a) 的频率档数应被截到 8
            a_section = r.stdout.split("[cpu1]", 1)[0]
            self.assertEqual(a_section.count("频率档 @"), 8)
            # ref_clk_hz 那一档必须保留
            self.assertIn("240000000Hz（基准", r.stdout)

    def test_dvfs_points_without_anchor_warns_clearly(self):
        """ref_clk_hz==0 但表里声明了 operating_points_hz（DVFS 点）→ 告警文案
        须明确指出「缺锚点、无法频率缩放」，而非泛泛的「未声明」。"""
        with tempfile.TemporaryDirectory() as tmp:
            d = make_a()
            d.update(ref_clk_hz=0, operating_points_hz=[160000000, 240000000])
            p = os.path.join(tmp, d["sched_name"] + ".json")
            with open(p, "w", encoding="utf-8") as f:
                json.dump(d, f)
            r = subprocess.run([sys.executable, TOOL, p], capture_output=True, text=True, encoding="utf-8")
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn("锚点", r.stdout)  # 明确提示缺锚点，无法缩放

    def test_schema_error_exit2(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = os.path.join(tmp, "bad.json")
            with open(p, "w", encoding="utf-8") as f:
                json.dump({"schema_version": 99}, f)
            r = subprocess.run([sys.executable, TOOL, p], capture_output=True, text=True)
            self.assertEqual(r.returncode, 2)

    def test_html_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "map.html")
            r = run_tool(tmp, "--html", out)
            self.assertEqual(r.returncode, 0, r.stderr)
            with open(out, encoding="utf-8") as f:
                html = f.read()
            self.assertIn("<!doctype html>", html.lower())
            self.assertIn("<svg", html)
            self.assertNotIn("http://", html)
            self.assertNotIn("https://", html)
            self.assertIn("sched_fixture_a", html)
            self.assertIn("sched_fixture_b", html)
            self.assertIn("20000", html)
            self.assertGreaterEqual(html.count("<rect"), 10)
            # 多频率表：HTML 里应含频率对比表，基准/理论两档都要出现
            self.assertIn("频率对比表", html)
            self.assertIn("240000000Hz", html)
            self.assertIn("80000000Hz", html)
            self.assertIn("estimated", html)

class InterferenceMathTest(unittest.TestCase):
    def test_ceiling_single_source(self):
        # minor=1000, period=300 -> ceil(1000/300)=4; wcet=10 -> 40
        r = smt._interference([{"name":"a","period_us":300,"wcet_us":10,"tier":"scheduled"}], 1000)
        self.assertEqual(r["total"], 40)
        self.assertEqual(r["scheduled"], 40)
        self.assertEqual(r["hardware"], 0)
        self.assertEqual(r["bad"], [])

    def test_multi_source_tier_split(self):
        srcs = [{"name":"hw","period_us":500,"wcet_us":6,"tier":"hardware"},   # ceil(1000/500)=2 ->12
                {"name":"sc","period_us":1000,"wcet_us":20,"tier":"scheduled"}] # ceil(1)=1 ->20
        r = smt._interference(srcs, 1000)
        self.assertEqual(r["hardware"], 12)
        self.assertEqual(r["scheduled"], 20)
        self.assertEqual(r["total"], 32)

    def test_bad_period_skipped(self):
        r = smt._interference([{"name":"z","period_us":0,"wcet_us":9,"tier":"scheduled"}], 1000)
        self.assertEqual(r["total"], 0)
        self.assertEqual(r["bad"], ["z"])

    def test_freq_scaled_wcet(self):
        # ref=240M, f=120M -> wcet ×2; period 不变
        r = smt._interference([{"name":"a","period_us":500,"wcet_us":10,"tier":"scheduled"}],
                              1000, ref=240000000, f_hz=120000000)
        # ceil(1000/500)=2 × ceil(10×240/120)=20 -> 40
        self.assertEqual(r["total"], 40)

    def test_analyze_empty_interference_noop(self):
        """opt-in 回归：不声明 interference_sources 时，analyze 的新键必须是
        no-op（intf 全 0、eff==raw、feasible=True），且不产生干扰相关告警。"""
        d = make_a()  # fixture 无 interference_sources
        per_table, warns, _ = smt.analyze([d], [], 80)
        a = per_table[0]
        self.assertEqual(a["intf"], {"hardware": 0, "scheduled": 0, "total": 0, "bad": []})
        self.assertEqual(a["eff_peak_us"], a["peak"]["isr_load_us"])
        self.assertIs(a["feasible"], True)
        for w in warns:
            self.assertNotIn("干扰", w)


if __name__ == "__main__":
    unittest.main()
