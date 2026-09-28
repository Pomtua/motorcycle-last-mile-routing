#!/usr/bin/env python3

import argparse
import csv
import json
import math
import statistics
import sys
from collections import Counter
from pathlib import Path


GROUP_FIELDS = (
    "spatial_class",
    "demand_class",
    "window_class",
    "size",
)
SOLVERS = (
    "i1_ls_zoned",
    "ortools_split_zoned",
)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Validate and summarize penalty-calibration JSONL."
        )
    )
    parser.add_argument(
        "input",
        type=Path,
        help="Calibration JSONL file",
    )
    parser.add_argument(
        "--csv",
        required=True,
        type=Path,
        help="New CSV summary file",
    )
    parser.add_argument(
        "--json",
        required=True,
        type=Path,
        help="New JSON summary file",
    )
    return parser.parse_args()


def require_number(
    result: dict,
    field: str,
) -> float:
    value = result.get(field)
    if (
        isinstance(value, bool) or
        not isinstance(value, (int, float)) or
        not math.isfinite(value)
    ):
        raise ValueError(
            f"invalid or missing result field: {field}"
        )
    return float(value)


def mean_or_none(values: list[float]) -> float | None:
    return statistics.mean(values) if values else None


def median_or_none(values: list[float]) -> float | None:
    return statistics.median(values) if values else None


def max_or_none(values: list[float]) -> float | None:
    return max(values) if values else None


def load_records(
    input_path: Path,
) -> tuple[
    dict[str, dict],
    dict[tuple[str, str, float], dict],
    dict[str, dict],
    list[float],
]:
    if not input_path.is_file():
        raise ValueError(
            f"input does not exist: {input_path}"
        )

    baselines: dict[str, dict] = {}
    zoned: dict[tuple[str, str, float], dict] = {}
    metadata: dict[str, dict] = {}
    alpha_grid: list[float] | None = None

    with input_path.open(
        "r",
        encoding="utf-8",
    ) as input_file:
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
                    f"invalid JSON on line {line_number}: "
                    f"{error}"
                ) from error

            if not isinstance(record, dict):
                raise ValueError(
                    f"line {line_number} is not an object"
                )

            experiment = record.get("experiment")
            result = record.get("result")
            case = record.get("case")

            if (
                not isinstance(experiment, dict) or
                experiment.get("phase") !=
                    "penalty_calibration"
            ):
                raise ValueError(
                    f"line {line_number} has invalid "
                    "experiment metadata"
                )
            if not isinstance(result, dict):
                raise ValueError(
                    f"line {line_number} has no parsed result"
                )
            if record.get("return_code") not in (0, 1):
                raise ValueError(
                    f"line {line_number} has unexpected "
                    "return code"
                )

            instance = experiment.get("instance")
            if not isinstance(instance, str):
                raise ValueError(
                    f"line {line_number} has invalid instance"
                )

            current_grid = experiment.get("alpha_grid")
            if not isinstance(current_grid, list):
                raise ValueError(
                    f"line {line_number} has no alpha grid"
                )
            try:
                current_grid = [
                    float(value)
                    for value in current_grid
                ]
            except (TypeError, ValueError) as error:
                raise ValueError(
                    f"line {line_number} has invalid alpha grid"
                ) from error

            if alpha_grid is None:
                alpha_grid = current_grid
            elif current_grid != alpha_grid:
                raise ValueError(
                    "records contain different alpha grids"
                )

            current_metadata = {
                field: experiment.get(field)
                for field in GROUP_FIELDS
            }
            if instance in metadata:
                if metadata[instance] != current_metadata:
                    raise ValueError(
                        f"inconsistent metadata for {instance}"
                    )
            else:
                metadata[instance] = current_metadata

            alpha = experiment.get("penalty_alpha")
            if case == "i1_ls":
                if alpha is not None:
                    raise ValueError(
                        "baseline record has penalty alpha"
                    )
                if instance in baselines:
                    raise ValueError(
                        f"duplicate baseline for {instance}"
                    )
                if (
                    result.get("solved") is not True or
                    result.get("valid") is not True
                ):
                    raise ValueError(
                        f"invalid baseline for {instance}"
                    )
                baselines[instance] = record
                continue

            if case not in SOLVERS:
                raise ValueError(
                    f"unexpected calibration case: {case}"
                )
            if (
                isinstance(alpha, bool) or
                not isinstance(alpha, (int, float)) or
                not math.isfinite(alpha)
            ):
                raise ValueError(
                    f"invalid alpha for {instance}"
                )

            key = (instance, case, float(alpha))
            if key in zoned:
                raise ValueError(
                    f"duplicate zoned record: {key}"
                )
            zoned[key] = record

    if alpha_grid is None:
        raise ValueError("input contains no records")

    instances = set(metadata)
    if set(baselines) != instances:
        raise ValueError(
            "not every instance has one baseline"
        )

    for instance in instances:
        for alpha in alpha_grid:
            for solver in SOLVERS:
                if (instance, solver, alpha) not in zoned:
                    raise ValueError(
                        "missing calibration record: "
                        f"{instance}, {solver}, {alpha}"
                    )

    expected_count = (
        len(instances) *
        (1 + len(alpha_grid) * len(SOLVERS))
    )
    actual_count = len(baselines) + len(zoned)
    if actual_count != expected_count:
        raise ValueError(
            f"found {actual_count} records; "
            f"expected {expected_count}"
        )

    return baselines, zoned, metadata, alpha_grid


def is_valid_solution(record: dict) -> bool:
    result = record["result"]
    return (
        result.get("solved") is True and
        result.get("valid") is True
    )


def summarize_group(
    instances: list[str],
    solver: str,
    alpha: float,
    baselines: dict[str, dict],
    zoned: dict[tuple[str, str, float], dict],
) -> dict:
    records = [
        zoned[(instance, solver, alpha)]
        for instance in instances
    ]
    valid_records = [
        record
        for record in records
        if is_valid_solution(record)
    ]

    distance_vs_baseline = []
    route_zone_excess = []
    fragmentation = []
    zones_per_route = []
    runtimes = []

    for record in valid_records:
        instance = record["experiment"]["instance"]
        result = record["result"]
        baseline = baselines[instance]["result"]

        baseline_distance = require_number(
            baseline,
            "distance_cost",
        )
        distance = require_number(
            result,
            "distance_cost",
        )
        distance_vs_baseline.append(
            (distance / baseline_distance - 1.0) * 100.0
        )
        route_zone_excess.append(
            require_number(result, "route_zone_excess")
        )
        fragmentation.append(
            require_number(result, "zone_fragmentation")
        )
        zones_per_route.append(
            require_number(
                result,
                "avg_zones_per_route",
            )
        )
        runtimes.append(
            require_number(result, "runtime_ms")
        )

    alpha_zero_pairs = []
    excess_reductions = []

    for record in valid_records:
        instance = record["experiment"]["instance"]
        reference = zoned.get(
            (instance, solver, 0.0)
        )
        if (
            reference is None or
            not is_valid_solution(reference)
        ):
            continue

        current_result = record["result"]
        reference_result = reference["result"]

        current_distance = require_number(
            current_result,
            "distance_cost",
        )
        reference_distance = require_number(
            reference_result,
            "distance_cost",
        )
        alpha_zero_pairs.append(
            (current_distance / reference_distance - 1.0)
            * 100.0
        )

        current_excess = require_number(
            current_result,
            "route_zone_excess",
        )
        reference_excess = require_number(
            reference_result,
            "route_zone_excess",
        )
        if reference_excess > 0.0:
            excess_reductions.append(
                (
                    reference_excess - current_excess
                ) / reference_excess * 100.0
            )

    statuses = Counter(
        record["result"].get("status")
        for record in records
    )
    known_statuses = (1, 2, 3, 4, 6)

    return {
        "solver": solver,
        "alpha": alpha,
        "instance_count": len(records),
        "solved_count": sum(
            record["result"].get("solved") is True
            for record in records
        ),
        "valid_count": len(valid_records),
        "paired_alpha_zero_count":
            len(alpha_zero_pairs),
        "mean_distance_vs_i1_baseline_pct":
            mean_or_none(distance_vs_baseline),
        "median_distance_vs_i1_baseline_pct":
            median_or_none(distance_vs_baseline),
        "mean_distance_vs_solver_alpha_zero_pct":
            mean_or_none(alpha_zero_pairs),
        "mean_route_zone_excess":
            mean_or_none(route_zone_excess),
        "mean_excess_reduction_vs_alpha_zero_pct":
            mean_or_none(excess_reductions),
        "mean_zone_fragmentation":
            mean_or_none(fragmentation),
        "mean_zones_per_route":
            mean_or_none(zones_per_route),
        "median_runtime_ms":
            median_or_none(runtimes),
        "max_runtime_ms":
            max_or_none(runtimes),
        **{
            f"status_{status}_count":
                statuses[status]
            for status in known_statuses
        },
        "status_other_count": sum(
            count
            for status, count in statuses.items()
            if status not in known_statuses and
               status is not None
        ),
    }


def make_groups(
    metadata: dict[str, dict],
) -> list[tuple[str, str, list[str]]]:
    instances = sorted(metadata)
    groups = [
        ("overall", "all", instances)
    ]

    for field in GROUP_FIELDS:
        values = sorted(
            {
                metadata[instance][field]
                for instance in instances
            },
            key=str,
        )
        for value in values:
            groups.append(
                (
                    field,
                    str(value),
                    [
                        instance
                        for instance in instances
                        if metadata[instance][field] == value
                    ],
                )
            )

    return groups


def main() -> int:
    args = parse_arguments()

    input_path = args.input.resolve()
    csv_path = args.csv.resolve()
    json_path = args.json.resolve()

    if csv_path == json_path:
        raise ValueError(
            "CSV and JSON outputs must be different files"
        )
    for output in (csv_path, json_path):
        if output.exists():
            raise ValueError(
                f"output already exists: {output}"
            )
        output.parent.mkdir(
            parents=True,
            exist_ok=True,
        )

    baselines, zoned, metadata, alpha_grid = (
        load_records(input_path)
    )

    rows = []
    for group_by, group_value, instances in make_groups(
        metadata
    ):
        for solver in SOLVERS:
            for alpha in alpha_grid:
                row = summarize_group(
                    instances,
                    solver,
                    alpha,
                    baselines,
                    zoned,
                )
                row["group_by"] = group_by
                row["group_value"] = group_value
                rows.append(row)

    fieldnames = (
        "group_by",
        "group_value",
        "solver",
        "alpha",
        "instance_count",
        "solved_count",
        "valid_count",
        "paired_alpha_zero_count",
        "mean_distance_vs_i1_baseline_pct",
        "median_distance_vs_i1_baseline_pct",
        "mean_distance_vs_solver_alpha_zero_pct",
        "mean_route_zone_excess",
        "mean_excess_reduction_vs_alpha_zero_pct",
        "mean_zone_fragmentation",
        "mean_zones_per_route",
        "median_runtime_ms",
        "max_runtime_ms",
        "status_1_count",
        "status_2_count",
        "status_3_count",
        "status_4_count",
        "status_6_count",
        "status_other_count",
    )

    with csv_path.open(
        "x",
        encoding="utf-8",
        newline="",
    ) as csv_file:
        writer = csv.DictWriter(
            csv_file,
            fieldnames=fieldnames,
        )
        writer.writeheader()
        writer.writerows(rows)

    summary = {
        "summary_schema_version": 1,
        "source": str(input_path),
        "instance_count": len(metadata),
        "alpha_grid": alpha_grid,
        "rows": rows,
    }
    with json_path.open(
        "x",
        encoding="utf-8",
    ) as json_file:
        json.dump(
            summary,
            json_file,
            ensure_ascii=False,
            indent=2,
        )
        json_file.write("\n")

    print(
        "alpha i1_valid distance_delta_pct "
        "excess_reduction_pct",
    )
    for row in rows:
        if (
            row["group_by"] == "overall" and
            row["solver"] == "i1_ls_zoned"
        ):
            print(
                f"{row['alpha']:g} "
                f"{row['valid_count']}/"
                f"{row['instance_count']} "
                f"{row['mean_distance_vs_i1_baseline_pct']:.4f} "
                f"{row['mean_excess_reduction_vs_alpha_zero_pct']:.4f}"
            )

    print(f"Wrote {csv_path}")
    print(f"Wrote {json_path}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(2)