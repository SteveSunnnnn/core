#!/usr/bin/env python3
"""Run the headless renderer benchmark across quality tiers and collect results.

The renderer honours CORE_VALIDATION_FRAMES: it renders that many frames with
no window interaction, then writes a key=value report to CORE_GPU_REPORT. This
script drives that harness once per (tier, run) pair and reduces the reports
into a single CSV plus a console table.

Usage:
    python tools/bench/run_benchmark.py --exe build/dev-desktop/core_desktop.exe \
        --shaders build/shaders --frames 600 --runs 3 --out build/bench_results.csv
"""

import argparse
import csv
import os
import subprocess
import sys
from pathlib import Path

# Fields that must appear in every report for the run to be usable.
TIMING_KEYS = (
    "sampled_frames",
    "avg_frame_ms",
    "min_frame_ms",
    "max_frame_ms",
    "p95_frame_ms",
    "avg_gpu_ms",
    "avg_cpu_ms",
    "fps",
)
CONTEXT_KEYS = (
    "device",
    "swapchain_width",
    "swapchain_height",
    "frames_presented",
    "quality_tier",
    "msaa_samples",
    "fxaa",
    "dither",
    "depth_buffer",
    "hdr_target",
    "terrain_octaves",
    "pipeline_cache",
    "timing_supported",
    "draw_calls_last_frame",
    "validation_errors",
)


def parse_report(path: Path) -> dict:
    report = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "=" not in line:
            continue
        key, _, value = line.partition("=")
        report[key.strip()] = value.strip()
    return report


def run_once(exe: Path, shaders: Path, content: Path, frames: int, tier: str,
             report_path: Path) -> int:
    # The child runs with cwd set next to the executable so it finds its DLLs,
    # so every path handed to it must already be absolute.
    env = dict(os.environ)
    env.update({
        "CORE_CONTENT_ROOT": str(content),
        "CORE_SHADER_DIR": str(shaders),
        "CORE_VALIDATION_FRAMES": str(frames),
        "CORE_WINDOWED": "1",
        # Validation layers add large, non-representative CPU cost.
        "CORE_VULKAN_VALIDATION": "0",
        "CORE_GPU_REPORT": str(report_path),
        "CORE_RENDER_QUALITY": tier,
    })
    if report_path.exists():
        report_path.unlink()
    result = subprocess.run([str(exe)], env=env, cwd=exe.parent,
                            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                            timeout=900)
    if result.returncode != 0:
        sys.stderr.write(result.stderr.decode("utf-8", "replace")[-2000:])
    return result.returncode


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--shaders", required=True)
    parser.add_argument("--content", default="content/base")
    parser.add_argument("--frames", type=int, default=600)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--tiers", default="legacy,low,medium,high,ultra")
    parser.add_argument("--tag", default="optimized")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    exe = Path(args.exe).resolve()
    # Resolve content/shader paths against the invocation directory, not the
    # executable's directory, so relative arguments mean what the caller expects.
    shaders = Path(args.shaders).resolve()
    content = Path(args.content).resolve()
    out_path = Path(args.out).resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    raw_dir = out_path.parent / "bench_reports"
    raw_dir.mkdir(parents=True, exist_ok=True)

    rows = []
    for tier in [t.strip() for t in args.tiers.split(",") if t.strip()]:
        for run in range(1, args.runs + 1):
            report_path = raw_dir / f"{args.tag}_{tier}_run{run}.txt"
            code = run_once(exe, shaders, content, args.frames, tier, report_path)
            if code != 0 or not report_path.exists():
                print(f"  {tier} run{run}: FAILED (exit {code})")
                continue
            report = parse_report(report_path)
            missing = [k for k in TIMING_KEYS if k not in report]
            if missing:
                print(f"  {tier} run{run}: incomplete report, missing {missing}")
                continue
            row = {"tag": args.tag, "tier": tier, "run": run}
            for key in CONTEXT_KEYS:
                row[key] = report.get(key, "")
            for key in TIMING_KEYS:
                row[key] = report[key]
            rows.append(row)
            print(f"  {tier} run{run}: frame={row['avg_frame_ms']}ms "
                  f"gpu={row['avg_gpu_ms']}ms fps={row['fps']}")

    if not rows:
        raise SystemExit("no successful benchmark runs")

    fieldnames = ["tag", "tier", "run"] + list(CONTEXT_KEYS) + list(TIMING_KEYS)
    with out_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    print(f"\nwrote {len(rows)} rows to {out_path}")


if __name__ == "__main__":
    main()
