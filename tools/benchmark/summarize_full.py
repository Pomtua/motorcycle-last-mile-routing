#!/usr/bin/env python3

import argparse
import csv
import json
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path

from run_full import CASES, instance_key


GROUPINGS = ("overall", "demand_class", "size", "spatial_class", "time_window")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Summarize a full benchmark sweep produced by run_full.py.")
    parser.add_argument("output_dir", type=Path, help="Directory given to run_full.py --output-dir")
    return parser.parse_args()


def is_number(value) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def mean(values: list[float]):
    return statistics.fmean(values) if values else None


def median(values: list[float]):
    return statistics.median(values) if values else None


def instance_profile(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    fleet = data["fleet"]
    customers = data["nodes"][1:]
    total_weight = sum(node["demand_weight"] for node in customers)
    total_volume = sum(node["demand_volume"] for node in customers)
    size, demand, spatial, window, seed = instance_key(path)
    reference = data["meta"].get("difficulty", {}).get("reference_cost")
    return {
        "size": size,
        "demand_class": demand,
        "spatial_class": spatial,
        "time_window": window,
        "seed": seed,
        "fleet_size": fleet["size"],
        "weight_capacity": fleet["weight_capacity"],
        "volume_capacity": fleet["volume_capacity"],
        "total_weight": total_weight,
        "total_volume": total_volume,
        "reference_cost": reference if is_number(reference) and reference > 0 else None,
        "route_lower_bound": max(
            math.ceil(total_weight / fleet["weight_capacity"] - 1e-9),
            math.ceil(total_volume / fleet["volume_capacity"] - 1e-9),
            1,
        ),
    }


def load_sweep(output_dir: Path) -> list[dict]:
    rows = []
    for run_file in sorted((output_dir / "runs").glob("*.jsonl")):
        records = [json.loads(line) for line in run_file.read_text(encoding="utf-8").splitlines() if line]
        if not records:
            continue
        instance = Path(records[0]["instance"])
        profile = instance_profile(instance)
        by_case = {record["case"]: record for record in records}
        nn_result = (by_case.get("nn") or {}).get("result") or {}
        chunk_count = nn_result.get("chunk_count")
        ls_result = (by_case.get("i1_ls") or {}).get("result") or {}
        for case in CASES:
            record = by_case.get(case)
            result = (record or {}).get("result") or {}
            route_count = result.get("route_count")
            distance = result.get("distance_cost")
            reference = profile["reference_cost"]
            ls_distance = ls_result.get("distance_cost")
            valid = result.get("valid") is True
            row = {
                "instance": instance.stem.removesuffix("_instance"),
                "case": case,
                **profile,
                "chunk_count": chunk_count,
                "skipped": bool((record or {}).get("skipped")),
                "solved": result.get("solved") is True,
                "valid": valid,
                "status": result.get("status"),
                "stop_reason": result.get("stop_reason"),
                "distance_cost": distance if valid else None,
                "route_count": route_count if valid else None,
                "runtime_ms": result.get("runtime_ms"),
                "iterations": result.get("iterations"),
                "route_zone_excess": result.get("route_zone_excess") if valid else None,
                "gap_vs_reference_pct": (
                    (distance - reference) / reference * 100.0
                    if valid and is_number(distance) and is_number(reference) and reference > 0 else None
                ),
                "gap_vs_i1_ls_pct": (
                    (distance - ls_distance) / ls_distance * 100.0
                    if valid and is_number(distance) and is_number(ls_distance) and ls_distance > 0 else None
                ),
                "chunks_per_route": (
                    chunk_count / route_count
                    if valid and is_number(chunk_count) and is_number(route_count) and route_count > 0 else None
                ),
                "weight_fill": (
                    profile["total_weight"] / (route_count * profile["weight_capacity"])
                    if valid and is_number(route_count) and route_count > 0 else None
                ),
                "volume_fill": (
                    profile["total_volume"] / (route_count * profile["volume_capacity"])
                    if valid and is_number(route_count) and route_count > 0 else None
                ),
            }
            rows.append(row)
    return rows


def group_value(row: dict, grouping: str):
    return "all" if grouping == "overall" else row[grouping]


def summarize_solvers(rows: list[dict]) -> list[dict]:
    groups = defaultdict(list)
    for row in rows:
        for grouping in GROUPINGS:
            groups[(grouping, group_value(row, grouping), row["case"])].append(row)

    summary = []
    for (grouping, value, case), members in sorted(groups.items(), key=lambda item: (item[0][0], str(item[0][1]), CASES.index(item[0][2]))):
        valid = [row for row in members if row["valid"]]
        summary.append({
            "group_by": grouping,
            "group_value": value,
            "case": case,
            "instances": len(members),
            "skipped": sum(row["skipped"] for row in members),
            "solved": sum(row["solved"] for row in members),
            "valid": len(valid),
            "mean_gap_vs_reference_pct": mean([row["gap_vs_reference_pct"] for row in valid if row["gap_vs_reference_pct"] is not None]),
            "mean_gap_vs_i1_ls_pct": mean([row["gap_vs_i1_ls_pct"] for row in valid if row["gap_vs_i1_ls_pct"] is not None]),
            "median_runtime_ms": median([row["runtime_ms"] for row in members if is_number(row["runtime_ms"])]),
            "mean_route_count": mean([row["route_count"] for row in valid]),
            "mean_route_zone_excess": mean([row["route_zone_excess"] for row in valid if is_number(row["route_zone_excess"])]),
            "mean_iterations": mean([row["iterations"] for row in valid if is_number(row["iterations"])]),
        })
    return summary


def summarize_degeneracy(rows: list[dict]) -> list[dict]:
    groups = defaultdict(list)
    for row in rows:
        if row["case"] == "hgs":
            groups[row["demand_class"]].append(row)

    summary = []
    for demand, members in sorted(groups.items()):
        valid = [row for row in members if row["valid"]]
        summary.append({
            "demand_class": demand,
            "instances": len(members),
            "mean_chunks_per_customer": mean([row["chunk_count"] / row["size"] for row in members if is_number(row["chunk_count"])]),
            "mean_route_lower_bound_ratio": mean([row["route_lower_bound"] / row["size"] for row in members]),
            "mean_max_chunks_per_route": mean([row["chunk_count"] / row["route_lower_bound"] for row in members if is_number(row["chunk_count"])]),
            "mean_hgs_chunks_per_route": mean([row["chunks_per_route"] for row in valid if row["chunks_per_route"] is not None]),
            "mean_hgs_weight_fill": mean([row["weight_fill"] for row in valid if row["weight_fill"] is not None]),
            "mean_hgs_volume_fill": mean([row["volume_fill"] for row in valid if row["volume_fill"] is not None]),
            "mean_hgs_gain_vs_i1_ls_pct": mean([-row["gap_vs_i1_ls_pct"] for row in valid if row["gap_vs_i1_ls_pct"] is not None]),
        })
    return summary


def write_csv(path: Path, rows: list[dict]) -> None:
    if not rows:
        return
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def cell(value, digits: int = 2) -> str:
    if value is None:
        return "-"
    if isinstance(value, float):
        return f"{value:.{digits}f}"
    return str(value)


def print_table(title: str, rows: list[dict], columns: list[tuple[str, str]]) -> None:
    print(f"\n## {title}\n")
    print("| " + " | ".join(label for label, _ in columns) + " |")
    print("|" + "---|" * len(columns))
    for row in rows:
        print("| " + " | ".join(cell(row[key]) for _, key in columns) + " |")


def main() -> int:
    args = parse_arguments()
    output_dir = args.output_dir.resolve()
    rows = load_sweep(output_dir)
    if not rows:
        raise ValueError(f"no completed instances in {output_dir / 'runs'}")

    solvers = summarize_solvers(rows)
    degeneracy = summarize_degeneracy(rows)
    write_csv(output_dir / "instances.csv", rows)
    write_csv(output_dir / "summary.csv", solvers)
    write_csv(output_dir / "degeneracy.csv", degeneracy)
    (output_dir / "summary.json").write_text(
        json.dumps({"solvers": solvers, "degeneracy": degeneracy}, indent=2) + "\n", encoding="utf-8"
    )

    columns = [
        ("case", "case"), ("valid", "valid"), ("instances", "instances"),
        ("vs reference %", "mean_gap_vs_reference_pct"), ("vs I1+LS %", "mean_gap_vs_i1_ls_pct"),
        ("median ms", "median_runtime_ms"), ("routes", "mean_route_count"), ("zone excess", "mean_route_zone_excess"),
    ]
    print_table("Overall", [row for row in solvers if row["group_by"] == "overall"], columns)
    for demand in sorted({row["demand_class"] for row in rows}):
        print_table(f"Demand class {demand}",
                    [row for row in solvers if row["group_by"] == "demand_class" and row["group_value"] == demand], columns)
    print_table("Degeneracy (HGS solutions)", degeneracy, [
        ("demand", "demand_class"), ("instances", "instances"), ("chunks/customer", "mean_chunks_per_customer"),
        ("max chunks/route", "mean_max_chunks_per_route"), ("HGS chunks/route", "mean_hgs_chunks_per_route"),
        ("weight fill", "mean_hgs_weight_fill"), ("volume fill", "mean_hgs_volume_fill"),
        ("HGS gain vs I1+LS %", "mean_hgs_gain_vs_i1_ls_pct"),
    ])
    print(f"\nWrote instances.csv, summary.csv, degeneracy.csv and summary.json to {output_dir}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(2)
