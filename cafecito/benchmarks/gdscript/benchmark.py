#!/usr/bin/env python3
"""Runner and statistical comparator for the Cafecito GDScript benchmark suite."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import math
import platform
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
PROJECT_DIR = Path(__file__).resolve().parent
RESULTS_DIR = REPOSITORY_ROOT / "cafecito" / "benchmarks" / "results"


def _detect_default_binary(target: str) -> Path:
    system = platform.system().lower()
    machine = platform.machine().lower()

    if system == "darwin":
        godot_platform = "macos"
        arch = "arm64" if machine in ("arm64", "aarch64") else "x86_64"
    elif system == "linux":
        godot_platform = "linuxbsd"
        arch = "arm64" if machine in ("arm64", "aarch64") else "x86_64"
    else:
        raise RuntimeError(f"Unsupported host platform for automatic binary discovery: {system}")

    target_name = {
        "release": "template_release",
        "debug": "template_debug",
        "editor": "editor",
    }[target]
    candidate = REPOSITORY_ROOT / "bin" / f"godot.{godot_platform}.{target_name}.{arch}"
    if candidate.exists():
        return candidate
    raise FileNotFoundError(
        f"Could not find Godot binary at {candidate}. Pass --binary explicitly or build it with `task build:{target}`."
    )


def _git_metadata() -> dict[str, Any]:
    def _run_git(*args: str) -> str:
        completed = subprocess.run(
            ["git", *args],
            cwd=REPOSITORY_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        return completed.stdout.strip() if completed.returncode == 0 else ""

    sha = _run_git("rev-parse", "--short=10", "HEAD")
    branch = _run_git("rev-parse", "--abbrev-ref", "HEAD")
    dirty = bool(_run_git("status", "--porcelain"))
    return {"sha": sha, "branch": branch, "dirty": dirty}


def _godot_version_string(binary: Path) -> str:
    completed = subprocess.run(
        [str(binary), "--version"],
        cwd=REPOSITORY_ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    return completed.stdout.strip().splitlines()[-1] if completed.stdout.strip() else "unknown"


def _percentile(sorted_values: list[float], fraction: float) -> float:
    if not sorted_values:
        return 0.0
    if len(sorted_values) == 1:
        return sorted_values[0]
    position = (len(sorted_values) - 1) * fraction
    lower_index = int(math.floor(position))
    upper_index = int(math.ceil(position))
    if lower_index == upper_index:
        return sorted_values[lower_index]
    weight = position - lower_index
    return sorted_values[lower_index] * (1.0 - weight) + sorted_values[upper_index] * weight


def _summarize_samples(samples_usec: list[int], operations: int) -> dict[str, float]:
    sorted_usec = sorted(float(value) for value in samples_usec)
    median_usec = statistics.median(sorted_usec)
    mean_usec = statistics.fmean(sorted_usec)
    stdev_usec = statistics.stdev(sorted_usec) if len(sorted_usec) > 1 else 0.0
    q1_usec = _percentile(sorted_usec, 0.25)
    q3_usec = _percentile(sorted_usec, 0.75)
    p95_usec = _percentile(sorted_usec, 0.95)
    min_usec = sorted_usec[0]
    max_usec = sorted_usec[-1]
    cv_percent = (stdev_usec / mean_usec * 100.0) if mean_usec > 0.0 else 0.0
    ns_per_op = (median_usec * 1000.0 / operations) if operations > 0 else 0.0
    return {
        "median_ms": median_usec / 1000.0,
        "mean_ms": mean_usec / 1000.0,
        "min_ms": min_usec / 1000.0,
        "max_ms": max_usec / 1000.0,
        "q1_ms": q1_usec / 1000.0,
        "q3_ms": q3_usec / 1000.0,
        "p95_ms": p95_usec / 1000.0,
        "stdev_ms": stdev_usec / 1000.0,
        "cv_percent": cv_percent,
        "ns_per_op": ns_per_op,
    }


def _invoke_godot(
    binary: Path,
    extra_user_args: list[str],
    json_out: Path | None = None,
) -> subprocess.CompletedProcess[str]:
    command = [
        str(binary),
        "--headless",
        "--path",
        str(PROJECT_DIR),
        "--script",
        "res://bench_runner.gd",
        "--",
        *extra_user_args,
    ]
    if json_out is not None:
        command.extend(["--json-out", str(json_out)])
    return subprocess.run(command, cwd=REPOSITORY_ROOT, capture_output=True, text=True, check=False)


def _print_run_table(payload: dict[str, Any]) -> None:
    meta = payload["metadata"]
    print(
        f"\nCafecito GDScript Benchmarks | {meta['godot_version']} | "
        f"git {meta['git']['sha']}{'*' if meta['git']['dirty'] else ''} | "
        f"runs={meta['process_runs']} samples/run={meta['samples_per_run']} warmup={meta['warmup']}"
    )
    header = f"{'Benchmark':<32} {'Category':<11} {'Median ms':>10} {'Min ms':>9} {'P95 ms':>9} {'CV%':>6} {'ns/op':>9}"
    print("-" * len(header))
    print(header)
    print("-" * len(header))

    current_category = ""
    for entry in payload["benchmarks"]:
        category = entry["category"]
        if current_category and category != current_category:
            print("-" * len(header))
        current_category = category
        stats = entry["stats"]
        print(
            f"{entry['name']:<32} {category:<11} "
            f"{stats['median_ms']:>10.2f} {stats['min_ms']:>9.2f} "
            f"{stats['p95_ms']:>9.2f} {stats['cv_percent']:>5.1f}% "
            f"{stats['ns_per_op']:>9.1f}"
        )
    print("-" * len(header))


def _cmd_list(args: argparse.Namespace) -> int:
    binary = Path(args.binary) if args.binary else _detect_default_binary(args.target)
    user_args = ["--list"]
    if args.filter:
        user_args.extend(["--filter", args.filter])
    completed = _invoke_godot(binary, user_args)
    if completed.stdout:
        print(completed.stdout, end="")
    if completed.stderr:
        print(completed.stderr, file=sys.stderr, end="")
    return completed.returncode


def _cmd_run(args: argparse.Namespace) -> int:
    binary = Path(args.binary) if args.binary else _detect_default_binary(args.target)
    if not binary.exists():
        print(f"Error: binary not found: {binary}", file=sys.stderr)
        return 1

    user_args = [
        "--warmup",
        str(args.warmup),
        "--samples",
        str(args.samples),
        "--scale",
        str(args.scale),
    ]
    if args.filter:
        user_args.extend(["--filter", args.filter])

    aggregated: dict[str, dict[str, Any]] = {}
    ordered_names: list[str] = []
    engine_payload: dict[str, Any] = {}

    for run_index in range(args.runs):
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as temp_file:
            temp_path = Path(temp_file.name)
        try:
            completed = _invoke_godot(binary, user_args, json_out=temp_path)
            if completed.returncode != 0:
                print(
                    f"Benchmark process failed on run {run_index + 1} (exit {completed.returncode}):", file=sys.stderr
                )
                if completed.stdout:
                    print(completed.stdout, file=sys.stderr)
                if completed.stderr:
                    print(completed.stderr, file=sys.stderr)
                return completed.returncode

            raw_text = temp_path.read_text(encoding="utf-8")
            if not raw_text.strip():
                print(
                    f"Benchmark process exited 0 on run {run_index + 1} without writing results:",
                    file=sys.stderr,
                )
                if completed.stdout:
                    print(completed.stdout, file=sys.stderr)
                if completed.stderr:
                    print(completed.stderr, file=sys.stderr)
                return 1
            run_data = json.loads(raw_text)
        finally:
            temp_path.unlink(missing_ok=True)

        engine_payload = run_data
        for item in run_data["benchmarks"]:
            name = item["name"]
            if name not in aggregated:
                ordered_names.append(name)
                aggregated[name] = {
                    "name": name,
                    "category": item["category"],
                    "description": item["description"],
                    "operations": int(item["operations"]),
                    "checksum": int(item["checksum"]),
                    "samples_usec": list(item["samples_usec"]),
                    "run_medians_usec": [float(statistics.median(item["samples_usec"]))],
                }
            else:
                if int(item["checksum"]) != aggregated[name]["checksum"]:
                    print(
                        f"Error: checksum mismatch across runs for {name}: "
                        f"{aggregated[name]['checksum']} vs {item['checksum']}",
                        file=sys.stderr,
                    )
                    return 3
                aggregated[name]["samples_usec"].extend(item["samples_usec"])
                aggregated[name]["run_medians_usec"].append(float(statistics.median(item["samples_usec"])))

    benchmarks_out: list[dict[str, Any]] = []
    for name in ordered_names:
        entry = aggregated[name]
        entry["stats"] = _summarize_samples(entry["samples_usec"], entry["operations"])
        benchmarks_out.append(entry)

    git_info = _git_metadata()
    timestamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    payload = {
        "schema_version": 1,
        "metadata": {
            "timestamp_utc": timestamp,
            "label": args.label or "",
            "binary": str(binary),
            "target": args.target,
            "godot_version": _godot_version_string(binary),
            "debug_build": bool(engine_payload.get("debug_build", False)),
            "os_name": engine_payload.get("os_name", platform.system()),
            "processor_name": engine_payload.get("processor_name", platform.processor()),
            "processor_count": engine_payload.get("processor_count", 0),
            "git": git_info,
            "process_runs": args.runs,
            "warmup": args.warmup,
            "samples_per_run": args.samples,
            "scale": args.scale,
            "filter": args.filter or "",
        },
        "benchmarks": benchmarks_out,
    }

    _print_run_table(payload)

    if args.out:
        out_path = Path(args.out)
    else:
        RESULTS_DIR.mkdir(parents=True, exist_ok=True)
        suffix = f"-{args.label}" if args.label else ""
        sha_part = git_info["sha"] or "unknown"
        out_path = RESULTS_DIR / f"{timestamp}-{sha_part}-{args.target}{suffix}.json"

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"Saved results to {out_path}")
    return 0


def _geomean(values: list[float]) -> float:
    if not values:
        return 1.0
    return math.exp(sum(math.log(value) for value in values) / len(values))


def _cmd_compare(args: argparse.Namespace) -> int:
    baseline_path = Path(args.baseline)
    candidate_path = Path(args.candidate)
    baseline = json.loads(baseline_path.read_text(encoding="utf-8"))
    candidate = json.loads(candidate_path.read_text(encoding="utf-8"))

    candidate_by_name = {item["name"]: item for item in candidate["benchmarks"]}

    base_meta = baseline["metadata"]
    cand_meta = candidate["metadata"]
    print(f"\nBaseline : {baseline_path.name} ({base_meta['godot_version']}, git {base_meta['git']['sha']})")
    print(f"Candidate: {candidate_path.name} ({cand_meta['godot_version']}, git {cand_meta['git']['sha']})")

    header = (
        f"{'Benchmark':<32} {'Category':<11} {'Base ms':>9} {'Cand ms':>9} {'Speedup':>9} {'Delta':>8} {'Status':<10}"
    )
    print("-" * len(header))
    print(header)
    print("-" * len(header))

    all_speedups: list[float] = []
    category_speedups: dict[str, list[float]] = {}
    checksum_mismatches: list[str] = []
    current_category = ""

    for item in baseline["benchmarks"]:
        name = item["name"]
        if name not in candidate_by_name:
            continue
        cand_item = candidate_by_name[name]
        category = item["category"]
        if current_category and category != current_category:
            print("-" * len(header))
        current_category = category

        base_stats = item["stats"]
        cand_stats = cand_item["stats"]
        base_ms = float(base_stats["median_ms"])
        cand_ms = float(cand_stats["median_ms"])
        speedup = base_ms / cand_ms if cand_ms > 0.0 else 0.0
        delta_pct = ((cand_ms - base_ms) / base_ms * 100.0) if base_ms > 0.0 else 0.0

        iqr_separated = (cand_stats["q3_ms"] < base_stats["q1_ms"]) or (cand_stats["q1_ms"] > base_stats["q3_ms"])
        if int(item["checksum"]) != int(cand_item["checksum"]):
            status = "CHECKSUM!"
            checksum_mismatches.append(name)
        elif abs(delta_pct) < args.threshold or not iqr_separated:
            status = "noise"
        elif speedup > 1.0:
            status = "FASTER"
        else:
            status = "SLOWER"

        all_speedups.append(speedup)
        category_speedups.setdefault(category, []).append(speedup)
        print(
            f"{name:<32} {category:<11} {base_ms:>9.2f} {cand_ms:>9.2f} "
            f"{speedup:>8.2f}x {delta_pct:>+7.1f}% {status:<10}"
        )

    print("-" * len(header))
    print("Category geometric mean speedup:")
    for category, ratios in category_speedups.items():
        ratio = _geomean(ratios)
        pct = (1.0 / ratio - 1.0) * 100.0
        print(f"  {category:<14} {ratio:.3f}x ({pct:+.1f}% time)")
    overall = _geomean(all_speedups)
    overall_pct = (1.0 / overall - 1.0) * 100.0
    print(f"  {'OVERALL':<14} {overall:.3f}x ({overall_pct:+.1f}% time)")

    if checksum_mismatches:
        print(f"\nERROR: Checksum mismatches detected in: {', '.join(checksum_mismatches)}", file=sys.stderr)
        return 2
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Cafecito GDScript benchmark runner and comparator")
    subparsers = parser.add_subparsers(dest="command", required=True)

    list_parser = subparsers.add_parser("list", help="List available benchmarks")
    list_parser.add_argument("--binary", help="Path to Godot executable")
    list_parser.add_argument("--target", choices=("release", "debug", "editor"), default="release")
    list_parser.add_argument("--filter", default="", help="Substring filter on benchmark name or category")
    list_parser.set_defaults(func=_cmd_list)

    run_parser = subparsers.add_parser("run", help="Run benchmarks and record JSON results")
    run_parser.add_argument("--binary", help="Path to Godot executable")
    run_parser.add_argument("--target", choices=("release", "debug", "editor"), default="release")
    run_parser.add_argument("--filter", default="", help="Substring filter on benchmark name or category")
    run_parser.add_argument("--runs", type=int, default=2, help="Number of separate Godot process runs (default: 2)")
    run_parser.add_argument("--warmup", type=int, default=2, help="Warmup iterations per benchmark (default: 2)")
    run_parser.add_argument("--samples", type=int, default=7, help="Timed samples per process run (default: 7)")
    run_parser.add_argument("--scale", type=float, default=1.0, help="Iteration count multiplier (default: 1.0)")
    run_parser.add_argument("--label", default="", help="Optional label embedded in output filename and metadata")
    run_parser.add_argument("--out", help="Explicit output JSON path")
    run_parser.set_defaults(func=_cmd_run)

    compare_parser = subparsers.add_parser("compare", help="Compare two saved benchmark JSON files")
    compare_parser.add_argument("baseline", help="Baseline JSON path")
    compare_parser.add_argument("candidate", help="Candidate JSON path")
    compare_parser.add_argument(
        "--threshold",
        type=float,
        default=2.0,
        help="Minimum percent change to flag as FASTER/SLOWER (default: 2.0)",
    )
    compare_parser.set_defaults(func=_cmd_compare)

    parsed = parser.parse_args(argv)
    return int(parsed.func(parsed))


if __name__ == "__main__":
    raise SystemExit(main())
