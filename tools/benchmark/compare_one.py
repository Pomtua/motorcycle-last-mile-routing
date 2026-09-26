#!/usr/bin/env python3

import argparse
import json
import math
import subprocess
import sys
from pathlib import Path


RESULT_PREFIX = "RESULT_JSON "


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed < 1:
        raise argparse.ArgumentTypeError(
            "value must be a positive integer"
        )
    return parsed


def non_negative_int(value: str) -> int:
    parsed = int(value)
    if parsed < 0:
        raise argparse.ArgumentTypeError(
            "value must be a non-negative integer"
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
            "Run all comparison configurations sequentially "
            "for one routing instance using equal-time "
            "OR-Tools budgets."
        )
    )
    parser.add_argument(
        "instance",
        type=Path,
        help="Instance JSON file"
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        help="Engine build directory"
    )
    parser.add_argument(
        "--num-zones",
        type=positive_int,
        help=(
            "Override the number of zones; "
            "defaults to the instance fleet size"
        )
    )
    penalty_group = parser.add_mutually_exclusive_group(
        required=True
    )
    penalty_group.add_argument(
        "--penalty-alpha",
        type=non_negative_float,
        help=(
            "Zone-penalty multiplier applied to the "
            "distance-only cost per route"
        )
    )
    penalty_group.add_argument(
        "--zone-penalty",
        type=non_negative_int,
        help=(
            "Explicit penalty per extra zone; "
            "intended for diagnostic overrides"
        )
    )
    parser.add_argument(
        "--output",
        required=True,
        type=Path,
        help="New JSONL output file"
    )
    return parser.parse_args()


def parse_result(stdout: str) -> tuple[dict | None, str | None]:
    result_lines = [
        line[len(RESULT_PREFIX):]
        for line in stdout.splitlines()
        if line.startswith(RESULT_PREFIX)
    ]

    if len(result_lines) != 1:
        return (
            None,
            "expected exactly one RESULT_JSON line, "
            f"found {len(result_lines)}"
        )

    try:
        result = json.loads(result_lines[0])
    except json.JSONDecodeError as error:
        return None, f"invalid RESULT_JSON: {error}"

    if not isinstance(result, dict):
        return None, "RESULT_JSON must contain a JSON object"
    if result.get("schema_version") != 1:
        return None, "unsupported or missing result schema_version"

    return result, None


def run_case(
    name: str,
    command: list[str],
    budget_source_case: str | None = None,
    budget_source_runtime_ms: float | None = None,
    requested_time_limit_seconds: float | None = None,
    zone_configuration: dict | None = None
) -> dict:
    completed = subprocess.run(
        command,
        capture_output=True,
        check=False,
        text=True
    )
    result, parse_error = parse_result(completed.stdout)

    return {
        "benchmark_schema_version": 1,
        "case": name,
        "command": command,
        "return_code": completed.returncode,
        "budget_source_case": budget_source_case,
        "budget_source_runtime_ms": budget_source_runtime_ms,
        "requested_time_limit_seconds":
            requested_time_limit_seconds,
        "zone_configuration": zone_configuration,
        "result": result,
        "result_parse_error": parse_error,
        "stdout": completed.stdout,
        "stderr": completed.stderr
    }


def write_case(
    output_file,
    index: int,
    total: int,
    name: str,
    command: list[str],
    budget_source_case: str | None = None,
    budget_source_runtime_ms: float | None = None,
    requested_time_limit_seconds: float | None = None,
    zone_configuration: dict | None = None
) -> dict:
    print(
        f"[{index}/{total}] {name}",
        file=sys.stderr,
        flush=True
    )

    record = run_case(
        name,
        command,
        budget_source_case,
        budget_source_runtime_ms,
        requested_time_limit_seconds,
        zone_configuration
    )
    output_file.write(
        json.dumps(
            record,
            ensure_ascii=False,
            separators=(",", ":")
        )
        + "\n"
    )
    output_file.flush()

    if record["result"] is None:
        raise ValueError(
            f"{name}: {record['result_parse_error']}"
        )
    if record["return_code"] not in (0, 1):
        raise ValueError(
            f"{name}: unexpected return code "
            f"{record['return_code']}"
        )

    return record


def result_runtime_ms(record: dict) -> float:
    result = record["result"]
    runtime = result.get("runtime_ms")

    if (
        isinstance(runtime, bool) or
        not isinstance(runtime, (int, float)) or
        not math.isfinite(runtime) or
        runtime < 0.0
    ):
        raise ValueError(
            f"{record['case']}: invalid runtime_ms"
        )

    return float(runtime)


def instance_fleet_size(instance: Path) -> int:
    try:
        data = json.loads(
            instance.read_text(encoding="utf-8")
        )
    except json.JSONDecodeError as error:
        raise ValueError(
            f"invalid instance JSON: {error}"
        ) from error

    fleet = data.get("fleet")
    size = (
        fleet.get("size")
        if isinstance(fleet, dict)
        else None
    )

    if (
        isinstance(size, bool) or
        not isinstance(size, int) or
        size < 1
    ):
        raise ValueError(
            "instance fleet.size must be a positive integer"
        )

    return size


def result_distance_scale(record: dict) -> float:
    result = record["result"]
    distance = result.get("distance_cost")
    route_count = result.get("route_count")

    if (
        isinstance(distance, bool) or
        not isinstance(distance, (int, float)) or
        not math.isfinite(distance) or
        distance < 0.0
    ):
        raise ValueError(
            f"{record['case']}: invalid distance_cost"
        )

    if (
        isinstance(route_count, bool) or
        not isinstance(route_count, int) or
        route_count < 1
    ):
        raise ValueError(
            f"{record['case']}: invalid route_count"
        )

    return float(distance) / route_count


def ortools_budget_seconds(runtime_ms: float) -> float:
    return runtime_ms / 1000.0


def main() -> int:
    args = parse_arguments()

    project_root = Path(__file__).resolve().parents[2]
    build_dir = (
        args.build_dir.resolve()
        if args.build_dir is not None
        else project_root / "engine" / "build"
    )
    instance = args.instance.resolve()
    output = args.output.resolve()

    if not instance.is_file():
        raise ValueError(
            f"instance does not exist: {instance}"
        )

    num_zones = (
        args.num_zones
        if args.num_zones is not None
        else instance_fleet_size(instance)
    )

    if output.exists():
        raise ValueError(
            f"output already exists: {output}"
        )

    executables = {
        "nn": build_dir / "run_nn",
        "i1_ls": build_dir / "run_ls",
        "ortools": build_dir / "run_ortools"
    }
    for executable in executables.values():
        if not executable.is_file():
            raise ValueError(
                f"solver executable does not exist: "
                f"{executable}"
            )

    instance_arg = str(instance)
    zones_arg = str(num_zones)
    total_cases = 6

    output.parent.mkdir(parents=True, exist_ok=True)

    with output.open("x", encoding="utf-8") as output_file:
        write_case(
            output_file,
            1,
            total_cases,
            "nn",
            [
                str(executables["nn"]),
                instance_arg
            ]
        )

        distance_record = write_case(
            output_file,
            2,
            total_cases,
            "i1_ls",
            [
                str(executables["i1_ls"]),
                instance_arg
            ]
        )
        distance_runtime_ms = result_runtime_ms(
            distance_record
        )
        distance_scale = result_distance_scale(
            distance_record
        )

        if args.penalty_alpha is not None:
            zone_penalty = int(
                math.floor(
                    args.penalty_alpha *
                    distance_scale +
                    0.5
                )
            )
            penalty_policy = "distance_per_route"
        else:
            zone_penalty = args.zone_penalty
            penalty_policy = "explicit"

        penalty_arg = str(zone_penalty)
        zone_configuration = {
            "count_policy": (
                "explicit"
                if args.num_zones is not None
                else "fleet_size"
            ),
            "num_zones": num_zones,
            "penalty_policy": penalty_policy,
            "penalty_alpha": args.penalty_alpha,
            "distance_scale": (
                distance_scale
                if args.penalty_alpha is not None
                else None
            ),
            "resolved_penalty": zone_penalty
        }

        distance_budget_seconds = ortools_budget_seconds(
            distance_runtime_ms
        )
        distance_budget_arg = format(
            distance_budget_seconds,
            ".17g"
        )

        write_case(
            output_file,
            3,
            total_cases,
            "ortools_raw",
            [
                str(executables["ortools"]),
                instance_arg,
                distance_budget_arg,
                "--raw"
            ],
            "i1_ls",
            distance_runtime_ms,
            distance_budget_seconds
        )

        write_case(
            output_file,
            4,
            total_cases,
            "ortools_split",
            [
                str(executables["ortools"]),
                instance_arg,
                distance_budget_arg,
                "--split"
            ],
            "i1_ls",
            distance_runtime_ms,
            distance_budget_seconds
        )

        zoned_record = write_case(
            output_file,
            5,
            total_cases,
            "i1_ls_zoned",
            [
                str(executables["i1_ls"]),
                instance_arg,
                zones_arg,
                penalty_arg
            ],
            zone_configuration=zone_configuration
        )
        zoned_runtime_ms = result_runtime_ms(
            zoned_record
        )
        zoned_budget_seconds = ortools_budget_seconds(
            zoned_runtime_ms
        )
        zoned_budget_arg = format(
            zoned_budget_seconds,
            ".17g"
        )

        write_case(
            output_file,
            6,
            total_cases,
            "ortools_split_zoned",
            [
                str(executables["ortools"]),
                instance_arg,
                zoned_budget_arg,
                zones_arg,
                penalty_arg,
                "--split"
            ],
            "i1_ls_zoned",
            zoned_runtime_ms,
            zoned_budget_seconds,
            zone_configuration
        )

    print(
        f"Wrote {total_cases} records to {output}",
        file=sys.stderr
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(2)
