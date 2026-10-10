#!/usr/bin/env python3

import argparse
import json
import math
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

from compare_one import result_distance_scale, result_runtime_ms, run_case


INSTANCE_PATTERN = re.compile(r"^(?P<spatial>RC|R|C)_(?P<demand>D\d)_n(?P<size>\d+)_(?P<window>[a-z]+)_s(?P<seed>\d+)_instance\.json$")

CASES = (
    "nn",
    "i1_ls",
    "ortools_raw",
    "ortools_split",
    "i1_ls_zoned",
    "ortools_split_zoned",
    "hgs",
    "ortools_split_hgs_time",
    "hgs_zoned",
    "ortools_split_zoned_hgs_time",
)


def positive_float(value: str) -> float:
    parsed = float(value)
    if not math.isfinite(parsed) or parsed <= 0.0:
        raise argparse.ArgumentTypeError("value must be finite and positive")
    return parsed


def non_negative_float(value: str) -> float:
    parsed = float(value)
    if not math.isfinite(parsed) or parsed < 0.0:
        raise argparse.ArgumentTypeError("value must be finite and non-negative")
    return parsed


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run every solver configuration on every benchmark instance, sequentially and resumably."
    )
    parser.add_argument("--output-dir", required=True, type=Path, help="Directory for per-instance JSONL records")
    parser.add_argument("--penalty-alpha", required=True, type=non_negative_float,
                        help="Zone penalty multiplier applied to the I1+LS distance per route")
    parser.add_argument("--hgs-seconds-per-customer", type=positive_float, default=0.1,
                        help="HGS time budget per customer (default 0.1 s)")
    parser.add_argument("--hgs-seed", type=int, default=42, help="HGS random seed")
    parser.add_argument("--instances-dir", type=Path, help="Instance directory (default data/instances)")
    parser.add_argument("--build-dir", type=Path, help="Engine build directory (default engine/build)")
    parser.add_argument("--sizes", type=int, nargs="+", help="Only run these customer counts")
    parser.add_argument("--limit", type=int, help="Stop after this many instances in this invocation")
    return parser.parse_args()


def instance_key(path: Path) -> tuple:
    match = INSTANCE_PATTERN.match(path.name)
    if match is None:
        raise ValueError(f"unexpected instance file name: {path.name}")
    return (
        int(match["size"]),
        match["demand"],
        match["spatial"],
        match["window"],
        int(match["seed"]),
    )


def git_commit(project_root: Path) -> str | None:
    completed = subprocess.run(
        ["git", "-C", str(project_root), "rev-parse", "HEAD"],
        capture_output=True, text=True, check=False
    )
    return completed.stdout.strip() or None


def format_seconds(seconds: float) -> str:
    return format(seconds, ".17g")


def skipped(name: str, reason: str) -> dict:
    return {
        "benchmark_schema_version": 1,
        "case": name,
        "skipped": True,
        "skip_reason": reason,
        "result": None,
    }


def usable(record: dict | None) -> bool:
    return record is not None and record.get("result") is not None and record["result"].get("solved") is True


def run_instance(instance: Path, executables: dict, args: argparse.Namespace, log) -> list[dict]:
    records: list[dict] = []
    instance_arg = str(instance)
    customers = instance_key(instance)[0]
    hgs_budget = customers * args.hgs_seconds_per_customer
    hgs_budget_arg = format_seconds(hgs_budget)

    def run(name: str, command: list[str], **context) -> dict:
        log(name)
        record = run_case(name, command, **context)
        records.append(record)
        return record

    def add_skip(name: str, reason: str) -> None:
        log(f"{name} (skipped: {reason})")
        records.append(skipped(name, reason))

    run("nn", [executables["nn"], instance_arg])
    ls_record = run("i1_ls", [executables["i1_ls"], instance_arg])

    zone_penalty = None
    if usable(ls_record):
        ls_seconds = format_seconds(result_runtime_ms(ls_record) / 1000.0)
        ls_runtime_ms = result_runtime_ms(ls_record)
        run("ortools_raw", [executables["ortools"], instance_arg, ls_seconds, "--raw"],
            budget_source_case="i1_ls", budget_source_runtime_ms=ls_runtime_ms,
            requested_time_limit_seconds=float(ls_seconds))
        run("ortools_split", [executables["ortools"], instance_arg, ls_seconds, "--split"],
            budget_source_case="i1_ls", budget_source_runtime_ms=ls_runtime_ms,
            requested_time_limit_seconds=float(ls_seconds))
        zone_penalty = int(math.floor(args.penalty_alpha * result_distance_scale(ls_record) + 0.5))
    else:
        for name in ("ortools_raw", "ortools_split"):
            add_skip(name, "i1_ls did not produce a solution to set the budget")

    zone_configuration = None
    if zone_penalty is not None:
        zone_configuration = {
            "count_policy": "silhouette_2sqrt_n",
            "requested_num_zones": None,
            "penalty_policy": "distance_per_route",
            "penalty_alpha": args.penalty_alpha,
            "distance_scale": result_distance_scale(ls_record),
            "resolved_penalty": zone_penalty,
        }
        zoned_record = run("i1_ls_zoned", [executables["i1_ls"], instance_arg, "auto", str(zone_penalty)],
                           zone_configuration=zone_configuration)
        if usable(zoned_record):
            zoned_runtime_ms = result_runtime_ms(zoned_record)
            zoned_seconds = format_seconds(zoned_runtime_ms / 1000.0)
            run("ortools_split_zoned",
                [executables["ortools"], instance_arg, zoned_seconds, "auto", str(zone_penalty), "--split"],
                budget_source_case="i1_ls_zoned", budget_source_runtime_ms=zoned_runtime_ms,
                requested_time_limit_seconds=float(zoned_seconds), zone_configuration=zone_configuration)
        else:
            add_skip("ortools_split_zoned", "i1_ls_zoned did not produce a solution to set the budget")
    else:
        for name in ("i1_ls_zoned", "ortools_split_zoned"):
            add_skip(name, "no zone penalty without an i1_ls distance scale")

    hgs_budget_ms = hgs_budget * 1000.0
    run("hgs", [executables["hgs"], instance_arg, hgs_budget_arg, "--seed", str(args.hgs_seed)],
        requested_time_limit_seconds=hgs_budget)
    run("ortools_split_hgs_time", [executables["ortools"], instance_arg, hgs_budget_arg, "--split"],
        budget_source_case="hgs_budget", budget_source_runtime_ms=hgs_budget_ms,
        requested_time_limit_seconds=hgs_budget)

    if zone_penalty is not None:
        run("hgs_zoned",
            [executables["hgs"], instance_arg, hgs_budget_arg, "auto", str(zone_penalty), "--seed", str(args.hgs_seed)],
            requested_time_limit_seconds=hgs_budget, zone_configuration=zone_configuration)
        run("ortools_split_zoned_hgs_time",
            [executables["ortools"], instance_arg, hgs_budget_arg, "auto", str(zone_penalty), "--split"],
            budget_source_case="hgs_budget", budget_source_runtime_ms=hgs_budget_ms,
            requested_time_limit_seconds=hgs_budget, zone_configuration=zone_configuration)
    else:
        for name in ("hgs_zoned", "ortools_split_zoned_hgs_time"):
            add_skip(name, "no zone penalty without an i1_ls distance scale")

    return records


def main() -> int:
    args = parse_arguments()
    project_root = Path(__file__).resolve().parents[2]
    instances_dir = (args.instances_dir or project_root / "data" / "instances").resolve()
    build_dir = (args.build_dir or project_root / "engine" / "build").resolve()
    output_dir = args.output_dir.resolve()
    runs_dir = output_dir / "runs"

    executables = {
        "nn": build_dir / "run_nn",
        "i1_ls": build_dir / "run_ls",
        "ortools": build_dir / "run_ortools",
        "hgs": build_dir / "run_hgs",
    }
    for executable in executables.values():
        if not executable.is_file():
            raise ValueError(f"solver executable does not exist: {executable}")
    executables = {name: str(path) for name, path in executables.items()}

    instances = sorted(instances_dir.glob("*_instance.json"), key=instance_key)
    if args.sizes:
        instances = [path for path in instances if instance_key(path)[0] in set(args.sizes)]
    if not instances:
        raise ValueError(f"no instances selected in {instances_dir}")

    runs_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = output_dir / "manifest.json"
    settings = {
        "penalty_alpha": args.penalty_alpha,
        "hgs_seconds_per_customer": args.hgs_seconds_per_customer,
        "hgs_seed": args.hgs_seed,
        "instances_dir": str(instances_dir),
        "cases": list(CASES),
    }
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("settings") != settings:
            raise ValueError(f"settings differ from the existing run in {output_dir}; use a new --output-dir")
    else:
        manifest = {
            "benchmark": "full_sweep",
            "created_at": datetime.now(timezone.utc).isoformat(),
            "git_commit": git_commit(project_root),
            "settings": settings,
        }
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    for stale in runs_dir.glob("*.partial"):
        stale.unlink()

    pending = [path for path in instances if not (runs_dir / f"{path.stem}.jsonl").exists()]
    if args.limit is not None:
        pending = pending[:args.limit]
    done = len(instances) - len([path for path in instances if not (runs_dir / f"{path.stem}.jsonl").exists()])
    print(f"{len(instances)} instances selected, {done} already done, running {len(pending)}", file=sys.stderr)

    sweep_start = time.monotonic()
    for index, instance in enumerate(pending, start=1):
        instance_start = time.monotonic()

        def log(case: str) -> None:
            elapsed = time.monotonic() - sweep_start
            print(f"[{index}/{len(pending)}] {instance.stem} :: {case}  (sweep {elapsed / 60:.1f} min)",
                  file=sys.stderr, flush=True)

        records = run_instance(instance, executables, args, log)
        partial = runs_dir / f"{instance.stem}.partial"
        with partial.open("w", encoding="utf-8") as output:
            for record in records:
                record["instance"] = str(instance)
                output.write(json.dumps(record, ensure_ascii=False, separators=(",", ":")) + "\n")
        partial.rename(runs_dir / f"{instance.stem}.jsonl")
        print(f"  done in {time.monotonic() - instance_start:.1f} s", file=sys.stderr, flush=True)

    print(f"Finished {len(pending)} instances into {runs_dir}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(2)
