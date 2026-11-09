#!/usr/bin/env python3

import argparse
import csv
import sys
from collections import defaultdict
from pathlib import Path

from summarize_full import load_sweep, mean, median

PLAIN_CASES = ("hgs", "hgs_fixed", "ortools_split_hgs_time")
ZONED_CASES = ("hgs_zoned", "ortools_split_zoned_hgs_time")
CASES = PLAIN_CASES + ZONED_CASES
LABELS = {
    "hgs": "HGS dynamic",
    "hgs_fixed": "HGS fixed",
    "ortools_split_hgs_time": "OR-Tools",
    "hgs_zoned": "HGS zoned",
    "ortools_split_zoned_hgs_time": "OR-Tools zoned",
}
PAIRS = (
    ("hgs", "ortools_split_hgs_time"),
    ("hgs_fixed", "ortools_split_hgs_time"),
    ("hgs", "hgs_fixed"),
    ("hgs_zoned", "ortools_split_zoned_hgs_time"),
)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Compare HGS and OR-Tools across time budgets from several sweeps.")
    parser.add_argument("output_dir", type=Path, help="Directory for budgets.csv and budget_quality.png")
    parser.add_argument("--budget", action="append", required=True, metavar="SECONDS_PER_CUSTOMER=DIR[,DIR...]",
                        help="Sweep directories holding the runs of one budget; later directories replace the same case")
    parser.add_argument("--reference", type=Path, required=True,
                        help="Sweep directory whose i1_ls runs give the distance every gap is measured against")
    return parser.parse_args()


def parse_budgets(specs: list[str], reference: Path) -> dict[float, list[Path]]:
    budgets = {}
    for spec in specs:
        value, _, dirs = spec.partition("=")
        if not dirs:
            raise ValueError(f"--budget must look like 0.05=results/budget-0.05, got {spec!r}")
        budgets[float(value)] = [reference.resolve()] + [Path(d).resolve() for d in dirs.split(",")]
    return dict(sorted(budgets.items()))


def gap(first: dict, second: dict) -> float:
    return (first["distance_cost"] - second["distance_cost"]) / second["distance_cost"] * 100.0


def summarize(rows_by_budget: dict[float, list[dict]]) -> list[dict]:
    summary = []
    for budget, rows in rows_by_budget.items():
        groups = defaultdict(lambda: defaultdict(dict))
        for row in rows:
            for key in ("all", row["size"]):
                groups[key][row["instance"]][row["case"]] = row
        for key, instances in sorted(groups.items(), key=lambda item: (item[0] != "all", item[0] if item[0] == "all" else int(item[0]))):
            record = {"budget_seconds_per_customer": budget, "size": key, "instances": len(instances)}
            for case in CASES:
                members = [cases[case] for cases in instances.values() if case in cases]
                valid = [row for row in members if row["valid"]]
                record[f"{case}_valid"] = len(valid)
                record[f"{case}_mean_gap_vs_i1_ls_pct"] = mean([row["gap_vs_i1_ls_pct"] for row in valid if row["gap_vs_i1_ls_pct"] is not None])
                record[f"{case}_median_runtime_ms"] = median([row["runtime_ms"] for row in members if row["runtime_ms"] is not None])
                record[f"{case}_total_runtime_ms"] = sum(row["runtime_ms"] for row in members if row["runtime_ms"] is not None)
                if case in ZONED_CASES:
                    record[f"{case}_mean_route_zone_excess"] = mean([row["route_zone_excess"] for row in valid if row["route_zone_excess"] is not None])
            for candidate, baseline in PAIRS:
                gaps = [gap(cases[candidate], cases[baseline]) for cases in instances.values()
                        if candidate in cases and baseline in cases and cases[candidate]["valid"] and cases[baseline]["valid"]]
                name = f"{candidate}_vs_{baseline}"
                record[f"{name}_matched"] = len(gaps)
                record[f"{name}_wins"] = sum(value < -1e-9 for value in gaps)
                record[f"{name}_ties"] = sum(abs(value) <= 1e-9 for value in gaps)
                record[f"{name}_losses"] = sum(value > 1e-9 for value in gaps)
                record[f"{name}_mean_gap_pct"] = mean(gaps)
            summary.append(record)
    return summary


def plot(summary: list[dict], path: Path, cases: tuple[str, ...]) -> bool:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    if not any(record[f"{case}_mean_gap_vs_i1_ls_pct"] is not None for record in summary for case in cases):
        return False
    groups = []
    for record in summary:
        if record["size"] not in groups:
            groups.append(record["size"])
    columns = 4
    rows = (len(groups) + columns - 1) // columns
    figure, axes = plt.subplots(rows, columns, figsize=(4 * columns, 3.2 * rows), squeeze=False)
    for index, group in enumerate(groups):
        axis = axes[index // columns][index % columns]
        records = [record for record in summary if record["size"] == group]
        budgets = [record["budget_seconds_per_customer"] for record in records]
        for case in cases:
            values = [record[f"{case}_mean_gap_vs_i1_ls_pct"] for record in records]
            points = [(b, v) for b, v in zip(budgets, values) if v is not None]
            if points:
                axis.plot([b for b, _ in points], [v for _, v in points], marker="o", label=LABELS[case])
        axis.set_xscale("log")
        axis.set_xticks(budgets)
        axis.set_xticklabels([f"{b:g}" for b in budgets], fontsize=8)
        axis.minorticks_off()
        axis.axhline(0.0, color="grey", linewidth=0.8)
        axis.set_title("all sizes" if group == "all" else f"n = {group}")
        axis.set_xlabel("budget (s per customer)")
        axis.set_ylabel("gap vs I1+LS (%)")
        axis.grid(True, alpha=0.3)
    for index in range(len(groups), rows * columns):
        axes[index // columns][index % columns].axis("off")
    axes[0][0].legend(fontsize=8)
    figure.tight_layout()
    figure.savefig(path, dpi=150)
    plt.close(figure)
    return True


def cell(value, digits=2) -> str:
    if value is None:
        return "-"
    return f"{value:.{digits}f}" if isinstance(value, float) else str(value)


def main() -> int:
    args = parse_arguments()
    budgets = parse_budgets(args.budget, args.reference)
    rows_by_budget = {budget: [row for row in load_sweep(dirs) if row["case"] in CASES] for budget, dirs in budgets.items()}

    def covered(rows: list[dict]) -> set[str]:
        return {row["instance"] for row in rows if row["case"] == "hgs" and (row["solved"] or row["skipped"] or row["valid"] or row["runtime_ms"] is not None)}

    common = set.intersection(*(covered(rows) for rows in rows_by_budget.values()))
    if not common:
        raise ValueError("no instance has HGS runs in every budget")
    rows_by_budget = {budget: [row for row in rows if row["instance"] in common] for budget, rows in rows_by_budget.items()}

    summary = summarize(rows_by_budget)
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    with (output_dir / "budgets.csv").open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=list(summary[0].keys()))
        writer.writeheader()
        writer.writerows(summary)
    plot(summary, output_dir / "budget_quality.png", PLAIN_CASES)
    has_zones = plot(summary, output_dir / "budget_quality_zoned.png", ZONED_CASES)

    print(f"{len(common)} instances present in every budget\n")
    print("## Mean gap vs I1+LS (%), all sizes\n")
    print("| budget | " + " | ".join(LABELS[case] for case in CASES) + " | OR-Tools valid | OR-Tools zoned valid |")
    print("|---|" + "---|" * (len(CASES) + 2))
    for record in summary:
        if record["size"] == "all":
            print(f"| {record['budget_seconds_per_customer']:g} | "
                  + " | ".join(cell(record[f"{case}_mean_gap_vs_i1_ls_pct"]) for case in CASES)
                  + f" | {record['ortools_split_hgs_time_valid']}/{record['instances']}"
                  + f" | {record['ortools_split_zoned_hgs_time_valid']}/{record['instances']} |")
    print("\n## Wins / ties / losses (mean gap %), all sizes\n")
    print("| budget | " + " | ".join(f"{LABELS[a]} vs {LABELS[b]}" for a, b in PAIRS) + " |")
    print("|---|" + "---|" * len(PAIRS))
    for record in summary:
        if record["size"] == "all":
            cells = []
            for a, b in PAIRS:
                name = f"{a}_vs_{b}"
                cells.append(f"{record[name + '_wins']}/{record[name + '_ties']}/{record[name + '_losses']} ({cell(record[name + '_mean_gap_pct'])})")
            print(f"| {record['budget_seconds_per_customer']:g} | " + " | ".join(cells) + " |")
    print("\n## Mean zone excess, all sizes\n")
    print("| budget | " + " | ".join(LABELS[case] for case in ZONED_CASES) + " |")
    print("|---|" + "---|" * len(ZONED_CASES))
    for record in summary:
        if record["size"] == "all":
            print(f"| {record['budget_seconds_per_customer']:g} | "
                  + " | ".join(cell(record[f"{case}_mean_route_zone_excess"]) for case in ZONED_CASES) + " |")
    written = "budgets.csv, budget_quality.png" + (" and budget_quality_zoned.png" if has_zones else "")
    print(f"\nWrote {written} to {output_dir}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(2)
