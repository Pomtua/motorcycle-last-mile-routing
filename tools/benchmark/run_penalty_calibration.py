#!/usr/bin/env python3

import argparse
import json
import math
import sys
from pathlib import Path

from compare_one import (
    ortools_budget_seconds,
    result_distance_scale,
    result_runtime_ms,
    run_case,
)
from run_pilot import select_pilot_instances


DEFAULT_ALPHAS = (
    0.0,
    0.01,
    0.05,
    0.10,
    0.25,
    0.50,
    1.0,
    2.0,
)


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
            "Run the resumable zone-penalty calibration "
            "on the 36 stratified pilot instances."
        )
    )
    parser.add_argument(
        "--instances-dir",
        type=Path,
        help="Instance directory; defaults to data/instances",
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        help="Engine build directory",
    )
    parser.add_argument(
        "--alphas",
        nargs="+",
        type=non_negative_float,
        default=list(DEFAULT_ALPHAS),
        help="Penalty-alpha values to evaluate",
    )
    parser.add_argument(
        "--max-instances",
        type=positive_int,
        help="Use only the first N instances for smoke testing",
    )
    parser.add_argument(
        "--output",
        required=True,
        type=Path,
        help=(
            "Calibration JSONL file; an existing compatible "
            "file is resumed"
        ),
    )
    return parser.parse_args()


def alpha_key(alpha: float) -> str:
    return format(alpha, ".17g")


def record_key(record: dict) -> tuple[str, str, str]:
    experiment = record.get("experiment")
    if not isinstance(experiment, dict):
        raise ValueError(
            "existing record is missing experiment metadata"
        )

    instance = experiment.get("instance")
    case = record.get("case")
    alpha = experiment.get("penalty_alpha")

    if not isinstance(instance, str):
        raise ValueError(
            "existing record has invalid experiment instance"
        )
    if case not in (
        "i1_ls",
        "i1_ls_zoned",
        "ortools_split_zoned",
    ):
        raise ValueError(
            f"existing record has unexpected case: {case}"
        )

    if case == "i1_ls":
        if alpha is not None:
            raise ValueError(
                "baseline record must not have penalty_alpha"
            )
        resolved_alpha = "baseline"
    else:
        if (
            isinstance(alpha, bool) or
            not isinstance(alpha, (int, float)) or
            not math.isfinite(alpha) or
            alpha < 0.0
        ):
            raise ValueError(
                "zoned record has invalid penalty_alpha"
            )
        resolved_alpha = alpha_key(float(alpha))

    return instance, case, resolved_alpha


def load_existing_records(
    output: Path,
    selected_names: set[str],
    alpha_grid: list[float],
) -> dict[tuple[str, str, str], dict]:
    if not output.exists():
        return {}
    if not output.is_file():
        raise ValueError(
            f"output is not a regular file: {output}"
        )

    expected_alpha_grid = [
        alpha_key(alpha)
        for alpha in alpha_grid
    ]
    records = {}

    with output.open("r", encoding="utf-8") as input_file:
        for line_number, line in enumerate(
            input_file,
            start=1,
        ):
            if not line.strip():
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(
                    f"invalid JSON on output line {line_number}: "
                    f"{error}"
                ) from error

            if not isinstance(record, dict):
                raise ValueError(
                    f"output line {line_number} is not an object"
                )

            experiment = record.get("experiment")
            if (
                not isinstance(experiment, dict) or
                experiment.get("phase") !=
                    "penalty_calibration" or
                experiment.get("alpha_grid") !=
                    expected_alpha_grid
            ):
                raise ValueError(
                    "existing output is incompatible with "
                    "the requested calibration plan"
                )

            key = record_key(record)
            if key[0] not in selected_names:
                raise ValueError(
                    "existing output contains an instance "
                    "outside the requested selection"
                )
            if key in records:
                raise ValueError(
                    f"duplicate existing calibration record: {key}"
                )
            records[key] = record

    return records


def validate_record(
    record: dict,
    require_valid_solution: bool,
) -> None:
    if record["result"] is None:
        raise ValueError(
            f"{record['case']}: "
            f"{record['result_parse_error']}"
        )
    if record["return_code"] not in (0, 1):
        raise ValueError(
            f"{record['case']}: unexpected return code "
            f"{record['return_code']}"
        )

    if require_valid_solution:
        result = record["result"]
        if (
            result.get("solved") is not True or
            result.get("valid") is not True
        ):
            raise ValueError(
                f"{record['case']}: calibration source "
                "must produce a solved, valid solution"
            )


def append_record(
    output: Path,
    record: dict,
) -> None:
    with output.open("a", encoding="utf-8") as output_file:
        output_file.write(
            json.dumps(
                record,
                ensure_ascii=False,
                separators=(",", ":"),
            )
            + "\n"
        )
        output_file.flush()


def add_experiment(
    record: dict,
    instance: dict,
    instance_index: int,
    selected_count: int,
    alpha_grid: list[float],
    penalty_alpha: float | None,
) -> dict:
    record["experiment"] = {
        "phase": "penalty_calibration",
        "instance": instance["path"].name,
        "instance_index": instance_index,
        "selected_instance_count": selected_count,
        "spatial_class": instance["spatial_class"],
        "demand_class": instance["demand_class"],
        "size": instance["size"],
        "window_class": instance["window_class"],
        "seed": instance["seed"],
        "alpha_grid": [
            alpha_key(alpha)
            for alpha in alpha_grid
        ],
        "penalty_alpha": penalty_alpha,
    }
    return record


def ensure_matching_zones(
    local_record: dict,
    ortools_record: dict,
) -> None:
    fields = (
        "num_zones",
        "zone_count_policy",
        "zone_candidate_max",
        "zone_silhouette_score",
    )
    for field in fields:
        if (
            local_record["result"].get(field) !=
            ortools_record["result"].get(field)
        ):
            raise ValueError(
                "zoned solvers used different zone "
                f"selection metadata: {field}"
            )


def main() -> int:
    args = parse_arguments()

    alpha_grid = list(dict.fromkeys(args.alphas))
    if not alpha_grid:
        raise ValueError("at least one alpha is required")

    project_root = Path(__file__).resolve().parents[2]
    instances_dir = (
        args.instances_dir.resolve()
        if args.instances_dir is not None
        else project_root / "data" / "instances"
    )
    build_dir = (
        args.build_dir.resolve()
        if args.build_dir is not None
        else project_root / "engine" / "build"
    )
    output = args.output.resolve()

    if not instances_dir.is_dir():
        raise ValueError(
            f"instance directory does not exist: "
            f"{instances_dir}"
        )

    executables = {
        "i1_ls": build_dir / "run_ls",
        "ortools": build_dir / "run_ortools",
    }
    for executable in executables.values():
        if not executable.is_file():
            raise ValueError(
                f"solver executable does not exist: "
                f"{executable}"
            )

    planned_instances = select_pilot_instances(
        instances_dir
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

    missing = [
        instance["path"]
        for instance in selected_instances
        if not instance["path"].is_file()
    ]
    if missing:
        raise ValueError(
            f"{len(missing)} selected instance(s) are missing; "
            f"first missing instance: {missing[0]}"
        )

    selected_names = {
        instance["path"].name
        for instance in selected_instances
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    completed = load_existing_records(
        output,
        selected_names,
        alpha_grid,
    )

    total_records = (
        len(selected_instances) *
        (1 + 2 * len(alpha_grid))
    )

    def run_and_store(
        instance: dict,
        instance_index: int,
        case: str,
        alpha: float | None,
        command: list[str],
        require_valid: bool,
        **run_case_arguments,
    ) -> dict:
        key = (
            instance["path"].name,
            case,
            (
                "baseline"
                if alpha is None
                else alpha_key(alpha)
            ),
        )
        if key in completed:
            return completed[key]

        print(
            f"[{len(completed) + 1}/{total_records}] "
            f"instance={instance['path'].name} "
            f"case={case} "
            f"alpha={alpha if alpha is not None else '-'}",
            file=sys.stderr,
            flush=True,
        )

        record = run_case(
            case,
            command,
            **run_case_arguments,
        )
        validate_record(record, require_valid)
        add_experiment(
            record,
            instance,
            instance_index,
            len(selected_instances),
            alpha_grid,
            alpha,
        )
        append_record(output, record)
        completed[key] = record
        return record

    for instance_index, instance in enumerate(
        selected_instances,
        start=1,
    ):
        instance_arg = str(instance["path"].resolve())

        baseline = run_and_store(
            instance,
            instance_index,
            "i1_ls",
            None,
            [
                str(executables["i1_ls"]),
                instance_arg,
            ],
            True,
        )
        distance_scale = result_distance_scale(baseline)

        for alpha in alpha_grid:
            zone_penalty = int(
                math.floor(
                    alpha * distance_scale + 0.5
                )
            )
            penalty_arg = str(zone_penalty)
            zone_configuration = {
                "count_policy": "silhouette_2sqrt_n",
                "requested_num_zones": None,
                "penalty_policy": "distance_per_route",
                "penalty_alpha": alpha,
                "distance_scale": distance_scale,
                "resolved_penalty": zone_penalty,
            }

            zoned = run_and_store(
                instance,
                instance_index,
                "i1_ls_zoned",
                alpha,
                [
                    str(executables["i1_ls"]),
                    instance_arg,
                    "auto",
                    penalty_arg,
                ],
                True,
                zone_configuration=zone_configuration,
            )

            zoned_runtime_ms = result_runtime_ms(zoned)
            budget_seconds = ortools_budget_seconds(
                zoned_runtime_ms
            )

            ortools_zoned = run_and_store(
                instance,
                instance_index,
                "ortools_split_zoned",
                alpha,
                [
                    str(executables["ortools"]),
                    instance_arg,
                    format(budget_seconds, ".17g"),
                    "auto",
                    penalty_arg,
                    "--split",
                ],
                False,
                budget_source_case="i1_ls_zoned",
                budget_source_runtime_ms=zoned_runtime_ms,
                requested_time_limit_seconds=budget_seconds,
                zone_configuration=zone_configuration,
            )
            ensure_matching_zones(
                zoned,
                ortools_zoned,
            )

    if len(completed) != total_records:
        raise ValueError(
            f"calibration produced {len(completed)} records; "
            f"expected {total_records}"
        )

    print(
        f"Calibration output contains "
        f"{total_records} records: {output}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(2)