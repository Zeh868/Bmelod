#!/usr/bin/env python3
"""tools/measure_size.py — parse size output and generate resource budget table"""
import subprocess
import sys
import os
import shutil

EXE_EXT = ".exe" if sys.platform == "win32" else ""


def find_size_tool():
    """Find a suitable size tool."""
    candidates = ["size", "arm-none-eabi-size", "riscv-none-elf-size"]
    for c in candidates:
        path = shutil.which(c)
        if path:
            return path
    return None


def run_size(size_tool, elf_path):
    """Run size tool and return stdout."""
    result = subprocess.run([size_tool, elf_path],
                            capture_output=True, text=True)
    return result.stdout if result.returncode == 0 else result.stderr


def measure(name, cmake_args, size_tool=None):
    build_dir = f"build_measure_{name}"
    cmd = ["cmake", "-B", build_dir, "-DBOARD=native_sim"] + cmake_args
    subprocess.run(cmd, check=True)
    subprocess.run(["cmake", "--build", build_dir], check=True)

    print(f"=== {name} ===")

    if size_tool:
        # Prefer test_skeleton if available, otherwise any test exe
        candidates = [
            f"{build_dir}/tests/unit/Debug/test_skeleton{EXE_EXT}",
            f"{build_dir}/tests/unit/test_skeleton{EXE_EXT}",
            f"{build_dir}/test_skeleton{EXE_EXT}",
        ]
        elf_path = None
        for c in candidates:
            if os.path.exists(c):
                elf_path = c
                break

        if elf_path:
            print(run_size(size_tool, elf_path))
        else:
            print(f"Warning: could not find test executable in {build_dir}")
    else:
        print("Warning: no size tool found (tried 'size', 'arm-none-eabi-size', 'riscv-none-elf-size')")


if __name__ == "__main__":
    size_tool = find_size_tool()
    configs = [
        ("ultra",          ["-DBM_ULTRA_MODE=1"]),
        ("core",           ["-DBM_ENABLE_MODULE=OFF", "-DBM_ENABLE_WDG=OFF"]),
        ("core+module",    ["-DBM_ENABLE_MODULE=ON",  "-DBM_ENABLE_WDG=OFF"]),
        ("core+module+wdg", ["-DBM_ENABLE_MODULE=ON", "-DBM_ENABLE_WDG=ON"]),
    ]
    for name, args in configs:
        try:
            measure(name, args, size_tool)
        except Exception as e:
            print(f"=== {name} FAILED ===")
            print(e)
