#!/usr/bin/env python3
"""Interleaved A/B measurement of the baseline build against each quality tier.

Single-target benchmarks drift: GPU clocks, thermals and background load all
move between runs, and on this machine the drift is larger than the effects
being measured. Running baseline and candidate alternately cancels most of it,
so this script measures a fresh baseline sample beside every tier sample.

The baseline executable is the pre-optimisation renderer plus only the timing
instrumentation, built from `tools/bench/patch_baseline_timing.py`.

Usage:
    python tools/bench/ab_interleaved.py --tiers legacy,low,medium,high,ultra \
        --pairs 5 --frames 600
"""

import argparse
import json
import os
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build" / "dev-desktop"

BASELINE_EXE = BUILD / "core_desktop_baseline.exe"
OPTIMIZED_EXE = BUILD / "core_desktop.exe"
SHADERS_BASELINE = ROOT / "build" / "shaders_baseline"
SHADERS_OPTIMIZED = ROOT / "build" / "shaders"
CONTENT = ROOT / "content" / "base"

# The two builds name their GPU field differently; the baseline patch was kept
# minimal rather than renaming fields in the original renderer.
BASELINE_GPU_KEY = "gpu_avg"
OPTIMIZED_GPU_KEY = "avg_gpu_ms"


def run_renderer(exe: Path, shaders: Path, frames: int, tier: str, report: Path) -> None:
    env = dict(os.environ)
    env.update({
        "CORE_CONTENT_ROOT": str(CONTENT),
        "CORE_SHADER_DIR": str(shaders),
        "CORE_VALIDATION_FRAMES": str(frames),
        "CORE_WINDOWED": "1",
        "CORE_VULKAN_VALIDATION": "0",
        "CORE_GPU_REPORT": str(report),
        "CORE_RENDER_QUALITY": tier,
    })
    result = subprocess.run([str(exe)], env=env, cwd=BUILD, timeout=900,
                            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    if result.returncode != 0:
        sys.stderr.write(result.stderr.decode("utf-8", "replace")[-1500:])
        raise SystemExit(f"{exe.name} [{tier}] failed with exit {result.returncode}")


def read_field(report: Path, key: str) -> float:
    for line in report.read_text(encoding="utf-8", errors="replace").splitlines():
        name, _, value = line.partition("=")
        if name.strip() == key:
            return float(value)
    raise SystemExit(f"{report} is missing {key}")


def measure_baseline(frames: int, seq: int) -> float:
    report = ROOT / "build" / f"ab_base_{seq}.txt"
    run_renderer(BASELINE_EXE, SHADERS_BASELINE, frames, "legacy", report)
    return read_field(report, BASELINE_GPU_KEY)


def measure_tier(tier: str, frames: int, seq: int) -> float:
    report = ROOT / "build" / f"ab_{tier}_{seq}.txt"
    run_renderer(OPTIMIZED_EXE, SHADERS_OPTIMIZED, frames, tier, report)
    return read_field(report, OPTIMIZED_GPU_KEY)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tiers", default="legacy,low,medium,high,ultra")
    parser.add_argument("--pairs", type=int, default=5)
    parser.add_argument("--frames", type=int, default=600)
    parser.add_argument("--out", default="build/ab_interleaved.json")
    args = parser.parse_args()

    for path in (BASELINE_EXE, OPTIMIZED_EXE):
        if not path.exists():
            raise SystemExit(f"missing executable: {path}")

    tiers = [t.strip() for t in args.tiers.split(",") if t.strip()]
    results = {}

    for tier in tiers:
        base_samples = []
        tier_samples = []
        for pair in range(args.pairs):
            base = measure_baseline(args.frames, pair)
            candidate = measure_tier(tier, args.frames, pair)
            base_samples.append(base)
            tier_samples.append(candidate)
            print(f"  {tier:7s} pair {pair}: baseline={base:.4f} ms  tier={candidate:.4f} ms")

        base_median = statistics.median(base_samples)
        tier_median = statistics.median(tier_samples)
        delta = (tier_median - base_median) / base_median * 100.0
        results[tier] = {
            "baseline_samples": base_samples,
            "tier_samples": tier_samples,
            "baseline_median": base_median,
            "tier_median": tier_median,
            "delta_pct": delta,
        }
        print(f"{tier}: baseline={base_median:.4f} ms  tier={tier_median:.4f} ms  "
              f"delta={delta:+.2f}%\n")

    out_path = ROOT / args.out
    out_path.write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(f"wrote {out_path}")


if __name__ == "__main__":
    main()
