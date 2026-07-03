#!/usr/bin/env python3
"""schedule_map_tool 自测：构造 JSON → 总表/全局 LCM/告警/退出码/HTML。"""
import json, os, subprocess, sys, tempfile, unittest

TOOL = os.path.join(os.path.dirname(__file__), "..", "..", "tools", "schedule_map_tool.py")

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

    def test_op_scaling_overload_warn(self):
        with tempfile.TemporaryDirectory() as tmp:
            r = run_tool(tmp, "--op-hz", "20000000")
            self.assertEqual(r.returncode, 0)
            self.assertIn("WARN: [cpu0] sched_fixture_a @20000000Hz est 峰值 1200us > minor 1000us", r.stdout)

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
            html = open(out, encoding="utf-8").read()
            self.assertIn("<!doctype html>", html.lower())
            self.assertIn("<svg", html)
            self.assertNotIn("http://", html)
            self.assertNotIn("https://", html)
            self.assertIn("sched_fixture_a", html)
            self.assertIn("sched_fixture_b", html)
            self.assertIn("20000", html)
            self.assertGreaterEqual(html.count("<rect"), 10)

if __name__ == "__main__":
    unittest.main()
