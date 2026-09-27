#!/usr/bin/env python3

import argparse
import itertools
import json
import math
import subprocess
import sys
import tempfile
from pathlib import Path


SPATIAL_CLASSES = ("C", "R", "RC")
DEMAND_CLASSES = ("D1", "D2", "D3", "D4")
INSTANCE_SIZES = (20, 50, 100, 200, 500, 1000)
WINDOW_CLASSES = ("loose", "tight")
PILOT_SEED = 42
CASES_PER_COMPARISON = 6


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed < 1:
        raise argparse.ArgumentTypeError(
            "value must be a positive integer"
        )
    return parsed


def non_negative_float(value: str) -> float:
    parsed = float(value)
    if not math.isfinite(parsed) or parsed < 0.0:
        raise argparse.ArgumentTypeError(
            "value must be finite and non-negative"
        )
    return parsed


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run the sequential Phase 0 benchmark pilot "
            "on 36 stratified instances."
        )
    )
    parser.add_argument(
        "--instances-dir",
        type=Path,
        help="Instance directory; defaults to data/instances"
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        help="Engine build directory passed to compare_one.py"
    )
    parser.add_argument(
        "--penalty-alpha",
        required=True,
        type=non_negative_float,
        help="Provisional zone-penalty multiplier"
    )
    parser.add_argument(
        "--trials",
        type=positive_int,
        default=3,
        help="Trials per instance; defaults to 3"
    )
    parser.add_argument(
        "--max-instances",
        type=positive_int,
        help=(
            "Run only the first N selected instances "
            "for smoke testing"
        )
    )
    parser.add_argument(
        "--output",
        required=True,
        type=Path,
        help="New combined JSONL output file"
    )
    return parser.parse_args()


def select_pilot_instances(
    instances_dir: Path
) -> list[dict]:
    selected = []
    combinations = itertools.product(
        SPATIAL_CLASSES,
        WINDOW_CLASSES,
        INSTANCE_SIZES
    )

    for index, (spatial, window, size) in enumerate(
        combinations
    ):
        demand = DEMAND_CLASSES[index % len(DEMAND_CLASSES)]
        filename = (
            f"{spatial}_{demand}_n{size}_"
            f"{window}_s{PILOT_SEED}_instance.json"
        )
        selected.append(
            {
                "path": instances_dir / filename,
                "spatial_class": spatial,
                "demand_class": demand,
                "size": size,
                "window_class": window,
                "seed": PILOT_SEED
            }
        )

    return selected


def read_comparison_records(
    output: Path
) -> list[dict]:
    if not output.is_file():
        raise ValueError(
            "compare_one.py did not create its output file"
        )

    records = [
        json.loads(line)
        for line in output.read_text(
            encoding="utf-8"
        ).splitlines()
        if line
    ]

    if len(records) != CASES_PER_COMPARISON:
        raise ValueError(
            "compare_one.py produced "
            f"{len(records)} records instead of "
            f"{CASES_PER_COMPARISON}"
        )

    if not all(isinstance(record, dict) for record in records):
        raise ValueError(
            "compare_one.py output must contain JSON objects"
        )

    return records


def run_comparison(
    command: list[str],
    temporary_output: Path
) -> list[dict]:
    completed = subprocess.run(
        command,
        capture_output=True,
        check=False,
        text=True
    )

    if completed.returncode != 0:
        if completed.stdout:
            print(completed.stdout, file=sys.stderr)
        if completed.stderr:
            print(completed.stderr, file=sys.stderr)
        raise ValueError(
            "compare_one.py failed with return code "
            f"{completed.returncode}"
        )

    return read_comparison_records(temporary_output)


def main() -> int:
    args = parse_arguments()

    project_root = Path(__file__).resolve().parents[2]
    compare_one = (
        project_root /
        "tools" /
        "benchmark" /
        "compare_one.py"
    )
    instances_dir = (
        args.instances_dir.resolve()
        if args.instances_dir is not None
        else project_root / "data" / "instances"
    )
    build_dir = (
        args.build_dir.resolve()
        if args.build_dir is not None
        else None
    )
    output = args.output.resolve()

    if not compare_one.is_file():
        raise ValueError(
            f"compare_one.py does not exist: {compare_one}"
        )
    if not instances_dir.is_dir():
        raise ValueError(
            f"instance directory does not exist: "
            f"{instances_dir}"
        )
    if output.exists():
        raise ValueError(
            f"output already exists: {output}"
        )

    planned_instances = select_pilot_instances(
        instances_dir
    )
    missing = [
        item["path"]
        for item in planned_instances
        if not item["path"].is_file()
    ]
    if missing:
        raise ValueError(
            f"{len(missing)} pilot instance(s) are missing; "
            f"first missing instance: {missing[0]}"
        )

    if (
        args.max_instances is not None and
        args.max_instances > len(planned_instances)
    ):
        raise ValueError(
            "--max-instances must not exceed "
            f"{len(planned_instances)}"
        )

    selected_instances = (
        planned_instances[:args.max_instances]
        if args.max_instances is not None
        else planned_instances
    )

    total_comparisons = (
        len(selected_instances) * args.trials
    )
    total_records = (
        total_comparisons * CASES_PER_COMPARISON
    )
    penalty_alpha_arg = format(
        args.penalty_alpha,
        ".17g"
    )

    output.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(
        prefix="routing-pilot-"
    ) as temporary_directory:
        temporary_root = Path(temporary_directory)

        with output.open(
            "x",
            encoding="utf-8"
        ) as output_file:
            comparison_index = 0

            for trial in range(1, args.trials + 1):
                for instance_index, instance in enumerate(
                    selected_instances,
                    start=1
                ):
                    comparison_index += 1
                    print(
                        "["
                        f"{comparison_index}/"
                        f"{total_comparisons}"
                        "] "
                        f"trial={trial} "
                        f"instance={instance['path'].name}",
                        file=sys.stderr,
                        flush=True
                    )

                    temporary_output = (
                        temporary_root /
                        f"comparison-{comparison_index}.jsonl"
                    )
                    command = [
                        sys.executable,
                        str(compare_one),
                        str(instance["path"]),
                        "--penalty-alpha",
                        penalty_alpha_arg,
                        "--output",
                        str(temporary_output)
                    ]
                    if build_dir is not None:
                        command.extend(
                            [
                                "--build-dir",
                                str(build_dir)
                            ]
                        )

                    records = run_comparison(
                        command,
                        temporary_output
                    )
                    experiment = {
                        "phase": "phase0_pilot",
                        "trial": trial,
                        "trials": args.trials,
                        "comparison_index": comparison_index,
                        "comparison_count": total_comparisons,
                        "instance_index": instance_index,
                        "planned_instance_count": len(
                            planned_instances
                        ),
                        "selected_instance_count": len(
                            selected_instances
                        ),
                        "spatial_class":
                            instance["spatial_class"],
                        "demand_class":
                            instance["demand_class"],
                        "size": instance["size"],
                        "window_class":
                            instance["window_class"],
                        "seed": instance["seed"],
                        "penalty_alpha":
                            args.penalty_alpha
                    }

                    for record in records:
                        record["experiment"] = experiment
                        output_file.write(
                            json.dumps(
                                record,
                                ensure_ascii=False,
                                separators=(",", ":")
                            )
                            + "\n"
                        )
                    output_file.flush()

    print(
        f"Wrote {total_records} records to {output}",
        file=sys.stderr
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(2)
