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
    parser.add_argument("output_dir", type=Path, help="Directory given to run_full.py --output-dir; summaries are written here")
    parser.add_argument("--runs-from", type=Path, nargs="+",
                        help="Sweep directories to merge in order, a later directory replacing the same case of an instance "
                             "(default: output_dir only)")
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


def merged_records(sweep_dirs: list[Path]) -> dict[str, dict[str, dict]]:
    merged: dict[str, dict[str, dict]] = {}
    for sweep_dir in sweep_dirs:
        for run_file in sorted((sweep_dir / "runs").glob("*.jsonl")):
            for line in run_file.read_text(encoding="utf-8").splitlines():
                if line:
                    record = json.loads(line)
                    merged.setdefault(run_file.name, {})[record["case"]] = record
    return merged


def load_sweep(sweep_dirs: list[Path]) -> list[dict]:
    rows = []
    for _, by_case in sorted(merged_records(sweep_dirs).items()):
        instance = Path(next(iter(by_case.values()))["instance"])
        profile = instance_profile(instance)
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
            splits = result.get("split_summary") or {}
            deliveries = splits.get("deliveries", chunk_count)
            row = {
                "instance": instance.stem.removesuffix("_instance"),
                "case": case,
                **profile,
                "chunk_count": chunk_count,
                "deliveries": deliveries if valid else None,
                "split_customers": splits.get("split_customers") if valid else None,
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
                "deliveries_per_route": (
                    deliveries / route_count
                    if valid and is_number(deliveries) and is_number(route_count) and route_count > 0 else None
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


DEGENERACY_CASES = ("hgs_fixed", "hgs")

PAIRS = (
    ("hgs", "hgs_fixed"),
    ("hgs", "ortools_split_hgs_time"),
    ("hgs_fixed", "ortools_split_hgs_time"),
    ("hgs_zoned", "ortools_split_zoned_hgs_time"),
)


def summarize_degeneracy(rows: list[dict]) -> list[dict]:
    groups = defaultdict(list)
    for row in rows:
        if row["case"] in DEGENERACY_CASES:
            groups[(row["demand_class"], row["case"])].append(row)

    summary = []
    for (demand, case), members in sorted(groups.items(), key=lambda item: (item[0][0], DEGENERACY_CASES.index(item[0][1]))):
        valid = [row for row in members if row["valid"]]
        if not valid:
            continue
        summary.append({
            "demand_class": demand,
            "case": case,
            "instances": len(members),
            "valid": len(valid),
            "mean_chunks_per_customer": mean([row["chunk_count"] / row["size"] for row in members if is_number(row["chunk_count"])]),
            "mean_route_lower_bound_ratio": mean([row["route_lower_bound"] / row["size"] for row in members]),
            "mean_max_chunks_per_route": mean([row["chunk_count"] / row["route_lower_bound"] for row in members if is_number(row["chunk_count"])]),
            "mean_deliveries_per_route": mean([row["deliveries_per_route"] for row in valid if row["deliveries_per_route"] is not None]),
            "mean_split_customer_ratio": mean([row["split_customers"] / row["size"] for row in valid if is_number(row["split_customers"])]),
            "mean_routes_over_lower_bound": mean([row["route_count"] / row["route_lower_bound"] for row in valid]),
            "mean_weight_fill": mean([row["weight_fill"] for row in valid if row["weight_fill"] is not None]),
            "mean_volume_fill": mean([row["volume_fill"] for row in valid if row["volume_fill"] is not None]),
            "mean_gain_vs_i1_ls_pct": mean([-row["gap_vs_i1_ls_pct"] for row in valid if row["gap_vs_i1_ls_pct"] is not None]),
        })
    return summary


def summarize_pairs(rows: list[dict]) -> list[dict]:
    by_instance = defaultdict(dict)
    for row in rows:
        by_instance[row["instance"]][row["case"]] = row

    summary = []
    for candidate, baseline in PAIRS:
        groups = defaultdict(list)
        for cases in by_instance.values():
            first, second = cases.get(candidate), cases.get(baseline)
            if first is None or second is None or not first["valid"] or not second["valid"]:
                continue
            for grouping in ("overall", "demand_class"):
                groups[(grouping, group_value(first, grouping))].append((first, second))

        for (grouping, value), matched in sorted(groups.items(), key=lambda item: (item[0][0] != "overall", str(item[0][1]))):
            gaps = [
                (first["distance_cost"] - second["distance_cost"]) / second["distance_cost"] * 100.0
                for first, second in matched
            ]
            summary.append({
                "candidate": candidate,
                "baseline": baseline,
                "group_by": grouping,
                "group_value": value,
                "matched": len(matched),
                "wins": sum(gap < -1e-9 for gap in gaps),
                "ties": sum(abs(gap) <= 1e-9 for gap in gaps),
                "losses": sum(gap > 1e-9 for gap in gaps),
                "mean_gap_pct": mean(gaps),
                "median_gap_pct": median(gaps),
                "mean_route_difference": mean([first["route_count"] - second["route_count"] for first, second in matched]),
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
    sweep_dirs = [path.resolve() for path in (args.runs_from or [output_dir])]
    rows = load_sweep(sweep_dirs)
    if not rows:
        raise ValueError(f"no completed instances in {', '.join(str(path / 'runs') for path in sweep_dirs)}")
    output_dir.mkdir(parents=True, exist_ok=True)

    solvers = summarize_solvers(rows)
    degeneracy = summarize_degeneracy(rows)
    pairs = summarize_pairs(rows)
    write_csv(output_dir / "instances.csv", rows)
    write_csv(output_dir / "summary.csv", solvers)
    write_csv(output_dir / "degeneracy.csv", degeneracy)
    write_csv(output_dir / "pairs.csv", pairs)
    (output_dir / "summary.json").write_text(
        json.dumps({"solvers": solvers, "degeneracy": degeneracy, "pairs": pairs}, indent=2) + "\n", encoding="utf-8"
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
    print_table("Pairwise at equal time (gap = candidate vs baseline, negative is better)", pairs, [
        ("candidate", "candidate"), ("baseline", "baseline"), ("group", "group_value"), ("matched", "matched"),
        ("W", "wins"), ("T", "ties"), ("L", "losses"), ("mean gap %", "mean_gap_pct"),
        ("median gap %", "median_gap_pct"), ("route diff", "mean_route_difference"),
    ])
    print_table("Degeneracy (HGS solutions)", degeneracy, [
        ("demand", "demand_class"), ("case", "case"), ("valid", "valid"), ("chunks/customer", "mean_chunks_per_customer"),
        ("max chunks/route", "mean_max_chunks_per_route"), ("deliveries/route", "mean_deliveries_per_route"),
        ("split customers", "mean_split_customer_ratio"), ("routes / LB", "mean_routes_over_lower_bound"),
        ("weight fill", "mean_weight_fill"), ("volume fill", "mean_volume_fill"),
        ("gain vs I1+LS %", "mean_gain_vs_i1_ls_pct"),
    ])
    print(f"\nWrote instances.csv, summary.csv, degeneracy.csv, pairs.csv and summary.json to {output_dir}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(2)
